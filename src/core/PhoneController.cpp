#include "PhoneController.h"

#include "AccountsController.h"
#include "NetworkMonitor.h"
#include "PowerMonitor.h"
#include "UpdateChecker.h"
#include "provisioning/Provider.h"
#include "provisioning/Registry.h"
#include "TrayController.h"
#include "UrlDispatcher.h"
#include "AccountsManager.h"
#include "Account.h"
#include "CallsController.h"
#include "CallEntry.h"
#include "CallManager.h"
#include "ContactsManager.h"
#include "ContactImporter.h"
#include "LogBuffer.h"
#include "HistoryManager.h"
#include "LinesManager.h"
#include "MessagesManager.h"
#include "SettingsController.h"
#include "SettingsManager.h"
#include "SipEngine.h"
#include "models/AccountsModel.h"
#include "models/CallsModel.h"
#include "models/ContactsModel.h"
#include "models/HistoryModel.h"
#include "models/MessagesModel.h"
#include "models/ConversationsModel.h"
#include "models/LinesModel.h"
#include "persistence/Database.h"
#include "platform/Keychain_file.h"
#include "platform/Keychain_factory.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>
#include <QTimer>

#include <spdlog/spdlog.h>

namespace compactphone {

namespace {

QString appDataPath()
{
    const auto base =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(base);
    return base;
}

} // namespace

PhoneController::PhoneController(QObject *parent) : QObject(parent)
{
    const auto dataPath = appDataPath();

    m_db = std::make_unique<persistence::Database>();
    if (!m_db->open((dataPath + QStringLiteral("/compactphone.db")).toStdString())) {
        spdlog::error("PhoneController: Database open failed");
    }
    const bool forceFile =
        !qEnvironmentVariableIsEmpty("COMPACTPHONE_FORCE_FILE_KEYCHAIN");
    m_keychain = platform::makeKeychain(
        (dataPath + QStringLiteral("/credentials.enc")).toStdString(),
        forceFile);

    m_engine = std::make_unique<sip::SipEngine>();
    if (!m_engine->start(0)) {
        spdlog::error("PhoneController: SipEngine failed to start");
    }

    m_accounts = std::make_unique<sip::AccountsManager>(
        m_engine.get(), m_db.get(), m_keychain.get());
    m_accountsModel = std::make_unique<models::AccountsModel>(m_accounts.get(), this);

    m_calls = std::make_unique<sip::CallManager>(m_accounts.get());
    m_callsModel = std::make_unique<models::CallsModel>(m_calls.get(), this);

    m_contacts = std::make_unique<sip::ContactsManager>(m_db.get());
    m_contactsModel = std::make_unique<models::ContactsModel>(m_contacts.get(), this);
    m_historyMgr = std::make_unique<sip::HistoryManager>(m_db.get());
    m_historyModel = std::make_unique<models::HistoryModel>(m_historyMgr.get(), this);

    m_messagesMgr = std::make_unique<sip::MessagesManager>(m_db.get(), this);
    m_messagesModel = std::make_unique<models::MessagesModel>(m_messagesMgr.get(), this);
    m_conversationsModel = std::make_unique<models::ConversationsModel>(m_messagesMgr.get(), this);

    m_linesMgr = std::make_unique<sip::LinesManager>(m_db.get(), m_accounts.get(), this);
    m_linesModel = std::make_unique<models::LinesModel>(m_linesMgr.get(), this);
    connect(m_messagesMgr.get(), &sip::MessagesManager::messagesChanged,
            this, &PhoneController::unreadMessageCountChanged);

    m_settings = std::make_unique<sip::SettingsManager>(m_db.get());

    m_accountsController = std::make_unique<AccountsController>(
        m_accounts.get(), m_accountsModel.get(), m_engine.get(), this);
    m_settingsController = std::make_unique<SettingsController>(
        m_engine.get(), m_settings.get(), dataPath, this);
    m_callsController = std::make_unique<CallsController>(
        m_accounts.get(), m_calls.get(), m_callsModel.get(),
        m_historyMgr.get(), m_historyModel.get(),
        m_settingsController.get(),
        [this] {
            return m_accountsController ? m_accountsController->activeAccountId() : -1;
        },
        [this](const QString &text, int autoDismissMs) {
            postNotice(text, autoDismissMs);
        },
        this);

    connect(m_accountsController.get(), &AccountsController::registeredAccountCountChanged,
            this, &PhoneController::registeredAccountCountChanged);
    connect(m_accountsController.get(), &AccountsController::activeAccountIdChanged,
            this, &PhoneController::activeAccountIdChanged);
    connect(m_accountsController.get(), &AccountsController::activeAccountIdChanged,
            this, &PhoneController::voicemailStateChanged);

    // MWI notifications from PJSIP arrive on a worker thread — bounce to
    // the main thread before touching Qt state.
    if (m_accounts) {
        m_accounts->setOnMwiChanged(
            [this](sip::AccountId, sip::MwiState) {
                QMetaObject::invokeMethod(
                    this, [this] { emit voicemailStateChanged(); },
                    Qt::QueuedConnection);
            });
        m_accounts->setOnInstantMessage(
            [this](sip::AccountId aid, const std::string &from,
                   const std::string &body) {
                QMetaObject::invokeMethod(this,
                    [this, aid, from, body] {
                        if (!m_messagesMgr) return;
                        sip::Message m;
                        m.accountId = aid;
                        m.peerUri = from;
                        m.direction = sip::MessageDirection::Incoming;
                        m.body = body;
                        m.createdAtMs = QDateTime::currentMSecsSinceEpoch();
                        m_messagesMgr->append(m);
                        if (m_trayController) {
                            m_trayController->notify(
                                tr("New message from %1")
                                    .arg(QString::fromStdString(from)),
                                QString::fromStdString(body).left(160));
                        }
                    }, Qt::QueuedConnection);
            });
    }

    connect(m_callsController.get(), &CallsController::callStateChanged,
            this, &PhoneController::callStateChanged);
    connect(m_callsController.get(), &CallsController::incomingCallChanged,
            this, &PhoneController::incomingCallChanged);
    connect(m_callsController.get(), &CallsController::ringingChanged,
            m_settingsController.get(), &SettingsController::setRinging);

    connect(m_settingsController.get(), &SettingsController::logLevelChanged,
            this, &PhoneController::logLevelChanged);
    connect(m_settingsController.get(), &SettingsController::ringtoneEnabledChanged,
            this, &PhoneController::ringtoneEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::themeIdChanged,
            this, &PhoneController::themeIdChanged);
    connect(m_settingsController.get(), &SettingsController::audioDevicesChanged,
            this, &PhoneController::audioDevicesChanged);
    connect(m_settingsController.get(), &SettingsController::captureDeviceIdChanged,
            this, &PhoneController::captureDeviceIdChanged);
    connect(m_settingsController.get(), &SettingsController::playbackDeviceIdChanged,
            this, &PhoneController::playbackDeviceIdChanged);
    connect(m_settingsController.get(), &SettingsController::ringtonePathChanged,
            this, &PhoneController::ringtonePathChanged);
    connect(m_settingsController.get(), &SettingsController::dndEnabledChanged,
            this, &PhoneController::dndEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::autoAnswerEnabledChanged,
            this, &PhoneController::autoAnswerEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::autoAnswerDelayMsChanged,
            this, &PhoneController::autoAnswerDelayMsChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdAlwaysEnabledChanged,
            this, &PhoneController::cfwdAlwaysEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdAlwaysTargetChanged,
            this, &PhoneController::cfwdAlwaysTargetChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdBusyEnabledChanged,
            this, &PhoneController::cfwdBusyEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdBusyTargetChanged,
            this, &PhoneController::cfwdBusyTargetChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdNoAnswerEnabledChanged,
            this, &PhoneController::cfwdNoAnswerEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdNoAnswerTargetChanged,
            this, &PhoneController::cfwdNoAnswerTargetChanged);
    connect(m_settingsController.get(), &SettingsController::cfwdNoAnswerTimeoutMsChanged,
            this, &PhoneController::cfwdNoAnswerTimeoutMsChanged);
    connect(m_settingsController.get(), &SettingsController::autoRecordEnabledChanged,
            this, &PhoneController::autoRecordEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::enterpriseFeaturesEnabledChanged,
            this, &PhoneController::enterpriseFeaturesEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::crashReportingEnabledChanged,
            this, &PhoneController::crashReportingEnabledChanged);
    connect(m_settingsController.get(), &SettingsController::alwaysOnTopChanged,
            this, &PhoneController::alwaysOnTopChanged);

    connect(&m_noticeTimer, &QTimer::timeout, this, &PhoneController::dismissNotice);
    m_noticeTimer.setSingleShot(true);
    if (m_settingsController && m_callsController) {
        m_settingsController->setRinging(m_callsController->ringing());
    }

    // System tray + native notifications. Best-effort: if the platform
    // doesn't expose a tray (some Linux WMs, certain headless modes), the
    // app keeps working window-only.
    m_trayController = std::make_unique<TrayController>(this);
    if (m_trayController->isAvailable()) {
        connect(m_trayController.get(), &TrayController::showRequested,
                this, &PhoneController::trayShowRequested);
        connect(m_trayController.get(), &TrayController::hideRequested,
                this, &PhoneController::trayHideRequested);
        connect(m_trayController.get(), &TrayController::quitRequested,
                this, &PhoneController::requestQuit);
        connect(this, &PhoneController::incomingCallChanged, this, [this] {
            if (incomingCallId() >= 0) {
                m_trayController->notifyIncomingCall(incomingCallFrom());
            }
        });

        // First-run welcome notification. The macOS notification permission
        // prompt is shown by the OS the first time an app sends a system
        // notification, so we trigger one shortly after launch.
        QSettings settings;
        if (!settings.value(QStringLiteral("welcomeNotificationShown"),
                            false).toBool()) {
            QTimer::singleShot(2500, this, [this] {
                if (m_trayController) {
                    m_trayController->notify(
                        tr("Compact Phone is ready"),
                        tr("You'll get a notification here whenever a call "
                           "comes in. The app lives in the menu bar — closing "
                           "the window doesn't quit it."));
                }
                QSettings s;
                s.setValue(QStringLiteral("welcomeNotificationShown"), true);
            });
        }
    }

    // Re-register every enabled account whenever network reachability
    // returns or the transport medium changes (Wi-Fi -> Ethernet, etc).
    // PJSIP's bound source IP becomes stale on those events.
    m_networkMonitor = std::make_unique<NetworkMonitor>(this);
    connect(m_networkMonitor.get(), &NetworkMonitor::networkBack,
            this, [this] {
        if (!m_accounts) return;
        m_accounts->reregisterAllEnabled();
        if (m_trayController) {
            m_trayController->notify(
                tr("Network restored"),
                tr("Re-registering your SIP accounts."));
        }
    });
    connect(m_networkMonitor.get(), &NetworkMonitor::networkLost,
            this, [this] {
        postNotice(tr("Network connection lost — calls may drop"), 6000);
    });

    // System wake. On platforms where the OS reports a wake event, kick
    // every enabled account to re-REGISTER even when NetworkMonitor
    // didn't notice — some kernels keep the cached connectivity state
    // across sleep even though the SIP socket is dead.
    m_powerMonitor = std::make_unique<PowerMonitor>(this);
    connect(m_powerMonitor.get(), &PowerMonitor::wokeUp,
            this, [this] {
        if (!m_accounts) return;
        spdlog::info("PhoneController: woke from sleep; re-registering");
        m_accounts->reregisterAllEnabled();
    });

    // Update checker — fetches the Sparkle appcast on demand from
    // checkForUpdates(). Surfaces results via the standard notice channel
    // so it shows up in the existing snackbar.
    m_updateChecker = std::make_unique<UpdateChecker>(this);
    connect(m_updateChecker.get(), &UpdateChecker::updateAvailable,
            this, [this](const QString &v, const QUrl &) {
        postNotice(tr("Update available: %1").arg(v), 6000);
    });
    connect(m_updateChecker.get(), &UpdateChecker::upToDate,
            this, [this] {
        postNotice(tr("Compact Phone is up to date"), 4000);
    });
    connect(m_updateChecker.get(), &UpdateChecker::checkFailed,
            this, [this](const QString &reason) {
        postNotice(tr("Update check failed: %1").arg(reason), 5000);
    });

    // Auto-provisioning. The Registry owns every backend Provider; we wire
    // their signals here once so the QML side only sees PhoneController.
    m_provisioningRegistry = std::make_unique<provisioning::Registry>();
    for (const auto &id : m_provisioningRegistry->ids()) {
        auto *p = m_provisioningRegistry->find(id);
        if (!p) continue;
        connect(p, &provisioning::Provider::progress,
                this, [this, id](const QString &stage) {
            emit provisioningProgress(id, stage);
        });
        connect(p, &provisioning::Provider::provisioningFailed,
                this, [this, id](const QString &error) {
            postNotice(tr("Sign-in failed: %1").arg(error), 5000);
            emit provisioningFailed(id, error);
        });
        connect(p, &provisioning::Provider::provisioningSucceeded,
                this, [this, id](const QVariantMap &params) {
            const int newId = addAccount(params);
            if (newId < 0) {
                const QString reason = tr("Could not save the new account.");
                postNotice(tr("Sign-in failed: %1").arg(reason), 5000);
                emit provisioningFailed(id, reason);
                return;
            }
            postNotice(tr("Account added"), 4000);
            emit accountProvisioned(id, newId);
        });
        connect(p, &provisioning::Provider::authMethodsDiscovered,
                this, [this, id](const QString &host, const QVariantList &methods) {
            emit authMethodsDiscovered(id, host, methods);
        });
        connect(p, &provisioning::Provider::authMethodsFailed,
                this, [this, id](const QString &host, const QString &error) {
            emit authMethodsFailed(id, host, error);
        });
    }

    // Click-to-call. macOS hands the app sip:/sips:/tel:/callto: URIs via
    // QFileOpenEvent, which UrlDispatcher buffers + emits. We strip the
    // scheme for tel:/callto: (they don't carry a host) so the bare
    // number/extension can be normalized through the active account.
    auto handleUri = [this](const QString &raw) {
        const QString trimmed = raw.trimmed();
        if (trimmed.isEmpty()) return;
        QString target = trimmed;
        const QString lower = target.toLower();
        if (lower.startsWith(QStringLiteral("tel:"))) {
            target = target.mid(4);
        } else if (lower.startsWith(QStringLiteral("callto:"))) {
            target = target.mid(7);
        }
        // sip:/sips: stay intact — dial() will pass them through normalization.
        setDialerUri(target);
        dial(target);
        emit trayShowRequested();
    };
    auto *dispatcher = UrlDispatcher::instance();
    connect(dispatcher, &UrlDispatcher::uriOpened, this, handleUri);
    const QString pending = dispatcher->takePending();
    if (!pending.isEmpty()) {
        QTimer::singleShot(0, this, [handleUri, pending] { handleUri(pending); });
    }
}

PhoneController::~PhoneController()
{
    m_networkMonitor.reset();
    m_powerMonitor.reset();
    m_updateChecker.reset();
    m_provisioningRegistry.reset();
    m_trayController.reset();
    m_callsController.reset();
    m_settingsController.reset();
    m_accountsController.reset();
    m_linesModel.reset();
    m_linesMgr.reset();
    m_conversationsModel.reset();
    m_messagesModel.reset();
    m_messagesMgr.reset();
    m_historyModel.reset();
    m_historyMgr.reset();
    m_contactsModel.reset();
    m_contacts.reset();
    m_settings.reset();
    m_callsModel.reset();
    m_calls.reset();
    m_accountsModel.reset();
    m_accounts.reset();
    if (m_engine) m_engine->stop();
    m_engine.reset();
    m_keychain.reset();
    m_db.reset();
}

QString PhoneController::callState() const
{
    return m_callsController
        ? m_callsController->callState()
        : QStringLiteral("idle");
}

QAbstractListModel *PhoneController::accountsModel() const
{
    return m_accountsController ? m_accountsController->model() : nullptr;
}

QAbstractListModel *PhoneController::callsModel() const
{
    return m_callsController ? m_callsController->model() : nullptr;
}

int PhoneController::addAccount(const QVariantMap &params)
{
    return m_accountsController
        ? m_accountsController->addAccount(params)
        : sip::kInvalidAccountId;
}

QVariantMap PhoneController::accountSnapshot(int accountId) const
{
    return m_accountsController
        ? m_accountsController->accountSnapshot(accountId)
        : QVariantMap{};
}

bool PhoneController::removeAccount(int accountId)
{
    return m_accountsController
        && m_accountsController->removeAccount(accountId);
}

bool PhoneController::updateAccount(int accountId, const QVariantMap &params)
{
    return m_accountsController
        && m_accountsController->updateAccount(accountId, params);
}

bool PhoneController::setDefaultAccount(int accountId)
{
    return m_accountsController
        && m_accountsController->setDefaultAccount(accountId);
}

bool PhoneController::setAccountEnabled(int accountId, bool enabled)
{
    return m_accountsController
        && m_accountsController->setAccountEnabled(accountId, enabled);
}

void PhoneController::dial(const QString &uri)
{
    if (m_callsController) m_callsController->dial(uri);
}

int PhoneController::activeAccountId() const
{
    return m_accountsController ? m_accountsController->activeAccountId() : -1;
}

void PhoneController::setActiveAccountId(int id)
{
    if (m_accountsController) m_accountsController->setActiveAccountId(id);
}

void PhoneController::hangup(int callId)
{
    if (m_callsController) m_callsController->hangup(callId);
}

bool PhoneController::hold(int callId)
{
    return m_callsController && m_callsController->hold(callId);
}

bool PhoneController::unhold(int callId)
{
    return m_callsController && m_callsController->unhold(callId);
}

bool PhoneController::setMuted(int callId, bool muted)
{
    return m_callsController && m_callsController->setMuted(callId, muted);
}

bool PhoneController::sendDtmf(int callId, const QString &digits)
{
    return m_callsController && m_callsController->sendDtmf(callId, digits);
}

int PhoneController::incomingCallId() const
{
    return m_callsController ? m_callsController->incomingCallId() : -1;
}

QString PhoneController::incomingCallFrom() const
{
    return m_callsController ? m_callsController->incomingCallFrom() : QString{};
}

bool PhoneController::acceptIncoming()
{
    return m_callsController && m_callsController->acceptIncoming();
}

bool PhoneController::declineIncoming()
{
    return m_callsController && m_callsController->declineIncoming();
}

bool PhoneController::blindTransfer(int callId, const QString &targetUri)
{
    return m_callsController && m_callsController->blindTransfer(callId, targetUri);
}

bool PhoneController::attendedTransfer(int activeCallId, int destCallId)
{
    return m_callsController
        && m_callsController->attendedTransfer(activeCallId, destCallId);
}

bool PhoneController::mergeCalls(int activeCallId, int heldCallId)
{
    return m_callsController
        && m_callsController->mergeCalls(activeCallId, heldCallId);
}

bool PhoneController::startRecording(int callId)
{
    return m_callsController && m_callsController->startRecording(callId);
}

bool PhoneController::stopRecording(int callId)
{
    return m_callsController && m_callsController->stopRecording(callId);
}

bool PhoneController::isRecording(int callId) const
{
    return m_callsController && m_callsController->isRecording(callId);
}

bool PhoneController::autoRecordEnabled() const
{
    return m_settingsController && m_settingsController->autoRecordEnabled();
}

void PhoneController::setAutoRecordEnabled(bool enabled)
{
    if (m_settingsController) m_settingsController->setAutoRecordEnabled(enabled);
}

bool PhoneController::enterpriseFeaturesEnabled() const
{
    return m_settingsController
        && m_settingsController->enterpriseFeaturesEnabled();
}

void PhoneController::setEnterpriseFeaturesEnabled(bool enabled)
{
    if (m_settingsController)
        m_settingsController->setEnterpriseFeaturesEnabled(enabled);
}

bool PhoneController::crashReportingEnabled() const
{
    return m_settingsController
        && m_settingsController->crashReportingEnabled();
}

void PhoneController::setCrashReportingEnabled(bool enabled)
{
    if (m_settingsController)
        m_settingsController->setCrashReportingEnabled(enabled);
}

bool PhoneController::alwaysOnTop() const
{
    return m_settingsController && m_settingsController->alwaysOnTop();
}

void PhoneController::setAlwaysOnTop(bool enabled)
{
    if (m_settingsController) m_settingsController->setAlwaysOnTop(enabled);
}

QAbstractListModel *PhoneController::conversationsModel() const
{
    return m_conversationsModel.get();
}

QAbstractListModel *PhoneController::messagesModel() const
{
    return m_messagesModel.get();
}

QAbstractListModel *PhoneController::linesModel() const
{
    return m_linesModel.get();
}

int PhoneController::addWatchedLine(const QString &uri, const QString &label)
{
    if (!m_linesMgr || !m_accountsController) return -1;
    const auto aid = m_accountsController->activeAccountId();
    if (aid <= 0) return -1;
    return m_linesMgr->add(aid, uri.toStdString(), label.toStdString());
}

bool PhoneController::removeWatchedLine(int lineId)
{
    return m_linesMgr
        && m_linesMgr->remove(static_cast<sip::WatchedLineId>(lineId));
}

void PhoneController::dialLine(int lineId)
{
    if (!m_linesMgr) return;
    if (auto l = m_linesMgr->find(static_cast<sip::WatchedLineId>(lineId))) {
        dial(QString::fromStdString(l->uri));
    }
}

int PhoneController::unreadMessageCount() const
{
    return m_messagesMgr ? m_messagesMgr->unreadCount() : 0;
}

bool PhoneController::sendMessage(const QString &peerUri, const QString &body)
{
    if (!m_accounts || !m_messagesMgr || !m_accountsController) return false;
    if (peerUri.isEmpty() || body.isEmpty()) return false;
    const auto aid = m_accountsController->activeAccountId();
    if (aid <= 0) return false;

    // Persist first so the user sees their outgoing bubble even if the
    // network is flaky; PJSIP retransmits MESSAGE for us.
    sip::Message m;
    m.accountId = aid;
    m.peerUri = peerUri.toStdString();
    m.direction = sip::MessageDirection::Outgoing;
    m.body = body.toStdString();
    m.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m.read = true;
    m_messagesMgr->append(m);

    const bool ok = m_accounts->sendInstantMessage(
        aid, peerUri.toStdString(), body.toStdString());
    if (!ok) postNotice(tr("Message failed to send"));
    return ok;
}

void PhoneController::selectConversation(const QString &peerUri)
{
    if (m_messagesModel) m_messagesModel->setPeer(peerUri);
    markConversationRead(peerUri);
}

void PhoneController::markConversationRead(const QString &peerUri)
{
    if (m_messagesMgr) m_messagesMgr->markPeerRead(peerUri.toStdString());
}

int PhoneController::firstHeldCallId(int excludeCallId) const
{
    if (!m_calls) return -1;
    for (const auto &e : m_calls->snapshot()) {
        if (static_cast<int>(e.id) == excludeCallId) continue;
        if (e.held && e.state == sip::CallState::Confirmed) {
            return static_cast<int>(e.id);
        }
    }
    return -1;
}

void PhoneController::dismissNotice()
{
    if (m_notice.isEmpty()) return;
    m_notice.clear();
    m_noticeTimer.stop();
    emit noticeChanged();
}

void PhoneController::postNotice(const QString &text, int autoDismissMs)
{
    m_notice = text;
    emit noticeChanged();
    if (autoDismissMs > 0) m_noticeTimer.start(autoDismissMs);
}

QAbstractListModel *PhoneController::contactsModel() const
{
    return m_contactsModel.get();
}

QAbstractListModel *PhoneController::historyModel() const
{
    return m_historyModel.get();
}

void PhoneController::setDialerUri(const QString &u)
{
    if (m_dialerUri == u) return;
    m_dialerUri = u;
    emit dialerUriChanged();
}

int PhoneController::addContact(const QString &displayName,
                                const QString &sipUri,
                                const QString &phone)
{
    sip::Contact c;
    c.displayName = displayName.toStdString();
    c.sipUri = sipUri.toStdString();
    c.phone = phone.toStdString();
    const auto id = m_contacts->add(c);
    m_contactsModel->refresh();
    return id;
}

void PhoneController::checkForUpdates()
{
    if (!m_updateChecker) return;
    postNotice(tr("Checking for updates…"), 2500);
    m_updateChecker->check();
}

void PhoneController::provisionWithProvider(const QString &providerId,
                                            const QString &host,
                                            const QString &username,
                                            const QString &password)
{
    if (!m_provisioningRegistry) return;
    auto *p = m_provisioningRegistry->find(providerId);
    if (!p) {
        const QString msg = tr("Unknown provisioning provider: %1").arg(providerId);
        postNotice(msg, 5000);
        emit provisioningFailed(providerId, msg);
        return;
    }
    p->provision(host, username, password);
}

QVariantList PhoneController::provisioningProviders() const
{
    if (!m_provisioningRegistry) return {};
    return m_provisioningRegistry->descriptors();
}

void PhoneController::provisionWithProviderToken(const QString &providerId,
                                                 const QString &host,
                                                 const QString &accessToken)
{
    if (!m_provisioningRegistry) return;
    auto *p = m_provisioningRegistry->find(providerId);
    if (!p) {
        const QString msg = tr("Unknown provisioning provider: %1").arg(providerId);
        postNotice(msg, 5000);
        emit provisioningFailed(providerId, msg);
        return;
    }
    p->provisionWithToken(host, accessToken);
}

void PhoneController::discoverAuthMethods(const QString &providerId,
                                          const QString &host)
{
    if (!m_provisioningRegistry) return;
    auto *p = m_provisioningRegistry->find(providerId);
    if (!p) {
        emit authMethodsFailed(providerId, host,
                               tr("Unknown provisioning provider: %1").arg(providerId));
        return;
    }
    p->discoverAuthMethods(host);
}

QVariantMap PhoneController::streamStats(int callId) const
{
    QVariantMap m;
    if (!m_calls) return m;
    const auto s = m_calls->streamStats(static_cast<sip::CallId>(callId));
    m["mos"] = s.mos;
    m["lossPct"] = s.lossPct;
    m["rttMs"] = s.rttMs;
    m["jitterMs"] = s.jitterMs;
    return m;
}

QStringList PhoneController::recentLogLines() const
{
    return LogBuffer::instance().lines();
}

bool PhoneController::exportDiagnostics(const QString &path) const
{
    QFile out(path);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate
                  | QIODevice::Text)) {
        spdlog::warn("exportDiagnostics: cannot open {}: {}",
                     path.toStdString(),
                     out.errorString().toStdString());
        return false;
    }

    QTextStream w(&out);
    w << "Compact Phone diagnostics\n";
    w << "Version:     " << QCoreApplication::applicationVersion() << "\n";
    w << "Platform:    " << QSysInfo::prettyProductName() << " ("
      << QSysInfo::currentCpuArchitecture() << ")\n";
    w << "Kernel:      " << QSysInfo::kernelType() << " "
      << QSysInfo::kernelVersion() << "\n";
    w << "Qt:          " << qVersion() << "\n";
    w << "\n";

    w << "Accounts (passwords redacted):\n";
    if (m_accountsController) {
        auto *model = m_accountsController->model();
        if (model) {
            for (int i = 0; i < model->rowCount(); ++i) {
                const int id = model->data(model->index(i, 0),
                                           models::AccountsModel::IdRole)
                                   .toInt();
                const auto snap = m_accountsController->accountSnapshot(id);
                w << "  #" << snap.value("accountId").toInt() << " "
                  << snap.value("label").toString() << "  "
                  << snap.value("username").toString() << "@"
                  << snap.value("domain").toString() << "  "
                  << snap.value("transport").toString() << "\n";
            }
        }
    }
    w << "\n--- Recent log ---\n";
    w << LogBuffer::instance().asText() << "\n";
    return true;
}

int PhoneController::importContactsFromFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        spdlog::warn("importContactsFromFile: cannot open {}: {}",
                     path.toStdString(),
                     f.errorString().toStdString());
        return 0;
    }
    const QString text = QString::fromUtf8(f.readAll());
    ImportResult result;
    const QString lower = path.toLower();
    if (lower.endsWith(".csv")) {
        result = contact_import::parseCsv(text);
    } else {
        // Default to vCard for .vcf and anything else.
        result = contact_import::parseVCard(text);
    }

    int imported = 0;
    for (const auto &c : result.contacts) {
        sip::Contact sc;
        sc.displayName = c.displayName.toStdString();
        sc.sipUri = c.sipUri.toStdString();
        sc.phone = c.phone.toStdString();
        if (m_contacts->add(sc) != sip::kInvalidContactId) imported++;
    }
    if (imported > 0) m_contactsModel->refresh();
    spdlog::info("importContactsFromFile: imported {} contacts from {} "
                 "({} dropped)",
                 imported, path.toStdString(), result.errors);
    return imported;
}

bool PhoneController::updateContact(int contactId,
                                    const QString &displayName,
                                    const QString &sipUri,
                                    const QString &phone)
{
    auto cur = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!cur) return false;
    cur->displayName = displayName.toStdString();
    cur->sipUri = sipUri.toStdString();
    cur->phone = phone.toStdString();
    const bool ok = m_contacts->update(*cur);
    if (ok) m_contactsModel->refresh();
    return ok;
}

bool PhoneController::removeContact(int contactId)
{
    const bool ok = m_contacts->remove(static_cast<sip::ContactId>(contactId));
    if (ok) m_contactsModel->refresh();
    return ok;
}

bool PhoneController::setContactFavorite(int contactId, bool favorite)
{
    auto cur = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!cur) return false;
    if (cur->favorite == favorite) return true;
    cur->favorite = favorite;
    const bool ok = m_contacts->update(*cur);
    if (ok) m_contactsModel->refresh();
    return ok;
}

void PhoneController::dialContact(int contactId)
{
    auto c = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!c) return;
    setDialerUri(QString::fromStdString(c->sipUri));
}

void PhoneController::redialFromHistory(int historyId)
{
    auto h = m_historyMgr->findById(static_cast<sip::HistoryId>(historyId));
    if (!h) return;
    const QString uri = QString::fromStdString(h->remoteUri);
    setDialerUri(uri);
    dial(uri);
}

QString PhoneController::logLevel() const
{
    return m_settingsController
        ? m_settingsController->logLevel()
        : QStringLiteral("info");
}

void PhoneController::setLogLevel(const QString &lvl)
{
    if (m_settingsController) m_settingsController->setLogLevel(lvl);
}

bool PhoneController::ringtoneEnabled() const
{
    return m_settingsController && m_settingsController->ringtoneEnabled();
}

void PhoneController::setRingtoneEnabled(bool enabled)
{
    if (m_settingsController) m_settingsController->setRingtoneEnabled(enabled);
}

QString PhoneController::themeId() const
{
    return m_settingsController
        ? m_settingsController->themeId()
        : QStringLiteral("light");
}

void PhoneController::setThemeId(const QString &id)
{
    if (m_settingsController) m_settingsController->setThemeId(id);
}

int PhoneController::registeredAccountCount() const
{
    return m_accountsController ? m_accountsController->registeredAccountCount() : 0;
}

QVariantList PhoneController::audioInputs() const
{
    return m_settingsController ? m_settingsController->audioInputs() : QVariantList{};
}

QVariantList PhoneController::audioOutputs() const
{
    return m_settingsController ? m_settingsController->audioOutputs() : QVariantList{};
}

int PhoneController::captureDeviceId() const
{
    return m_settingsController ? m_settingsController->captureDeviceId() : -1;
}

int PhoneController::playbackDeviceId() const
{
    return m_settingsController ? m_settingsController->playbackDeviceId() : -1;
}

void PhoneController::setCaptureDeviceId(int id)
{
    if (m_settingsController) m_settingsController->setCaptureDeviceId(id);
}

void PhoneController::setPlaybackDeviceId(int id)
{
    if (m_settingsController) m_settingsController->setPlaybackDeviceId(id);
}

void PhoneController::refreshAudioDevices()
{
    if (m_settingsController) m_settingsController->refreshAudioDevices();
}

void PhoneController::testRingtone(int durationMs)
{
    if (m_settingsController) m_settingsController->testRingtone(durationMs);
}

void PhoneController::requestQuit()
{
    QCoreApplication::quit();
}

QString PhoneController::ringtonePath() const
{
    return m_settingsController ? m_settingsController->ringtonePath() : QString{};
}

void PhoneController::setRingtonePath(const QString &p)
{
    if (m_settingsController) m_settingsController->setRingtonePath(p);
}

QString PhoneController::defaultRingtonePath() const
{
    return m_settingsController
        ? m_settingsController->defaultRingtonePath()
        : appDataPath() + QStringLiteral("/ringtone.wav");
}

bool PhoneController::dndEnabled() const
{
    return m_settingsController && m_settingsController->dndEnabled();
}

void PhoneController::setDndEnabled(bool enabled)
{
    if (m_settingsController) m_settingsController->setDndEnabled(enabled);
}

bool PhoneController::autoAnswerEnabled() const
{
    return m_settingsController && m_settingsController->autoAnswerEnabled();
}

void PhoneController::setAutoAnswerEnabled(bool enabled)
{
    if (m_settingsController) m_settingsController->setAutoAnswerEnabled(enabled);
}

int PhoneController::autoAnswerDelayMs() const
{
    return m_settingsController ? m_settingsController->autoAnswerDelayMs() : 0;
}

void PhoneController::setAutoAnswerDelayMs(int ms)
{
    if (m_settingsController) m_settingsController->setAutoAnswerDelayMs(ms);
}

int PhoneController::newVoicemailCount() const
{
    if (!m_accounts || !m_accountsController) return 0;
    const auto aid = m_accountsController->activeAccountId();
    if (aid <= 0) return 0;
    return m_accounts->mwiStateOf(static_cast<sip::AccountId>(aid)).newMessages;
}

QString PhoneController::activeVoicemailNumber() const
{
    if (!m_accounts || !m_accountsController) return QString{};
    const auto aid = m_accountsController->activeAccountId();
    if (aid <= 0) return QString{};
    if (auto a = m_accounts->find(static_cast<sip::AccountId>(aid))) {
        return QString::fromStdString(a->voicemailNumber);
    }
    return QString{};
}

void PhoneController::dialVoicemail()
{
    const QString num = activeVoicemailNumber();
    if (!num.isEmpty()) dial(num);
}

bool PhoneController::cfwdAlwaysEnabled() const
{
    return m_settingsController && m_settingsController->cfwdAlwaysEnabled();
}
void PhoneController::setCfwdAlwaysEnabled(bool e)
{
    if (m_settingsController) m_settingsController->setCfwdAlwaysEnabled(e);
}
QString PhoneController::cfwdAlwaysTarget() const
{
    return m_settingsController ? m_settingsController->cfwdAlwaysTarget() : QString{};
}
void PhoneController::setCfwdAlwaysTarget(const QString &uri)
{
    if (m_settingsController) m_settingsController->setCfwdAlwaysTarget(uri);
}

bool PhoneController::cfwdBusyEnabled() const
{
    return m_settingsController && m_settingsController->cfwdBusyEnabled();
}
void PhoneController::setCfwdBusyEnabled(bool e)
{
    if (m_settingsController) m_settingsController->setCfwdBusyEnabled(e);
}
QString PhoneController::cfwdBusyTarget() const
{
    return m_settingsController ? m_settingsController->cfwdBusyTarget() : QString{};
}
void PhoneController::setCfwdBusyTarget(const QString &uri)
{
    if (m_settingsController) m_settingsController->setCfwdBusyTarget(uri);
}

bool PhoneController::cfwdNoAnswerEnabled() const
{
    return m_settingsController && m_settingsController->cfwdNoAnswerEnabled();
}
void PhoneController::setCfwdNoAnswerEnabled(bool e)
{
    if (m_settingsController) m_settingsController->setCfwdNoAnswerEnabled(e);
}
QString PhoneController::cfwdNoAnswerTarget() const
{
    return m_settingsController ? m_settingsController->cfwdNoAnswerTarget() : QString{};
}
void PhoneController::setCfwdNoAnswerTarget(const QString &uri)
{
    if (m_settingsController) m_settingsController->setCfwdNoAnswerTarget(uri);
}
int PhoneController::cfwdNoAnswerTimeoutMs() const
{
    return m_settingsController ? m_settingsController->cfwdNoAnswerTimeoutMs() : 20000;
}
void PhoneController::setCfwdNoAnswerTimeoutMs(int ms)
{
    if (m_settingsController) m_settingsController->setCfwdNoAnswerTimeoutMs(ms);
}

} // namespace compactphone

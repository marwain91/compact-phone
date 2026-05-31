#include "PhoneController.h"

#include "AccountsController.h"
#include "NetworkMonitor.h"
#include "PowerMonitor.h"
#include "UpdateChecker.h"
#include "TrayController.h"
#include "UrlDispatcher.h"
#include "AccountsManager.h"
#include "Account.h"
#include "CallsController.h"
#include "CallEntry.h"
#include "CallManager.h"
#include "ContactsManager.h"
#include "CrashReporting.h"
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
#include "platform/Keychain_factory.h"

#include <QCoreApplication>
#include <QDesktopServices>
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
    m_contactsController = std::make_unique<ContactsController>(
        m_contacts.get(), m_contactsModel.get(), this);
    // dialContact pre-fills the dialer; the controller stays dialer-agnostic.
    connect(m_contactsController.get(), &ContactsController::dialRequested,
            this, &PhoneController::setDialerUri);
    m_historyMgr = std::make_unique<sip::HistoryManager>(m_db.get());
    m_historyModel = std::make_unique<models::HistoryModel>(m_historyMgr.get(), this);

    m_messagesMgr = std::make_unique<sip::MessagesManager>(m_db.get(), this);
    m_messagesModel = std::make_unique<models::MessagesModel>(m_messagesMgr.get(), this);
    m_conversationsModel = std::make_unique<models::ConversationsModel>(m_messagesMgr.get(), this);

    m_linesMgr = std::make_unique<sip::LinesManager>(m_db.get(), m_accounts.get(), this);
    m_linesModel = std::make_unique<models::LinesModel>(m_linesMgr.get(), this);

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
    m_messagesController = std::make_unique<MessagesController>(
        m_accounts.get(), m_messagesMgr.get(),
        m_messagesModel.get(), m_conversationsModel.get(),
        [this] {
            return m_accountsController ? m_accountsController->activeAccountId() : -1;
        },
        [this](const QString &text) { postNotice(text); },
        this);
    m_linesController = std::make_unique<LinesController>(
        m_linesMgr.get(), m_linesModel.get(),
        [this] {
            return m_accountsController ? m_accountsController->activeAccountId() : -1;
        },
        this);
    // dialLine places a real call (trusted in-app action).
    connect(m_linesController.get(), &LinesController::callRequested,
            this, &PhoneController::dial);

    connect(m_accountsController.get(), &AccountsController::registeredAccountCountChanged,
            this, &PhoneController::registeredAccountCountChanged);
    connect(m_accountsController.get(), &AccountsController::activeAccountIdChanged,
            this, &PhoneController::activeAccountIdChanged);
    connect(m_accountsController.get(), &AccountsController::activeAccountIdChanged,
            this, &PhoneController::voicemailStateChanged);
    connect(m_accountsController.get(), &AccountsController::registrationFailed,
            this, [this](const QString &msg) { postNotice(msg, 6000); });

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
    // Settings change-notifications reach QML directly via
    // PhoneController.settings.<x> bindings — no re-emit through PhoneController.

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
            this, [this](const QString &v, const QUrl &url) {
        const bool changed =
            m_latestUpdateVersion != v || m_latestUpdateUrl != url;
        m_latestUpdateVersion = v;
        m_latestUpdateUrl = url;
        if (changed) {
            emit latestUpdateChanged();
        }
        postNotice(tr("Update available: %1").arg(v), 8000);
    });
    connect(m_updateChecker.get(), &UpdateChecker::upToDate,
            this, [this] {
        if (!m_latestUpdateVersion.isEmpty() || !m_latestUpdateUrl.isEmpty()) {
            m_latestUpdateVersion.clear();
            m_latestUpdateUrl = QUrl();
            emit latestUpdateChanged();
        }
        postNotice(tr("Compact Phone is up to date"), 4000);
    });
    connect(m_updateChecker.get(), &UpdateChecker::checkFailed,
            this, [this](const QString &reason) {
        postNotice(tr("Update check failed: %1").arg(reason), 5000);
    });

    // Auto-provisioning. The Registry owns every backend Provider; we wire
    // their signals here once so the QML side only sees PhoneController.
    m_provisioningController = std::make_unique<ProvisioningController>(
        [this](const QVariantMap &params) {
            return m_accountsController ? m_accountsController->addAccount(params) : -1;
        },
        [this](const QString &text, int autoDismissMs) {
            postNotice(text, autoDismissMs);
        },
        this);

    // External sip:/sips:/tel:/callto: URIs reach the app from an untrusted
    // source (web pages, documents; macOS delivers them via QFileOpenEvent ->
    // UrlDispatcher). We PRE-FILL the dialer and raise the window but do NOT
    // place the call — the user must press Call. Auto-dialing here would let a
    // malicious link silently call a premium-rate or attacker-controlled
    // number with the user's account. We strip the scheme for tel:/callto:
    // (no host) so the bare number/extension normalizes through the active
    // account; sip:/sips: stay intact and normalize when the user dials.
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
        setDialerUri(target);
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
    m_provisioningController.reset();
    m_trayController.reset();
    m_callsController.reset();
    m_settingsController.reset();
    m_accountsController.reset();
    m_contactsController.reset();
    m_messagesController.reset();
    m_linesController.reset();
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

SettingsController *PhoneController::settingsController() const
{
    return m_settingsController.get();
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

bool PhoneController::crashReportingAvailable() const
{
    return crash::configuredSentryAvailable();
}

MessagesController *PhoneController::messagesController() const
{
    return m_messagesController.get();
}

LinesController *PhoneController::linesController() const
{
    return m_linesController.get();
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

ContactsController *PhoneController::contactsController() const
{
    return m_contactsController.get();
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

void PhoneController::checkForUpdates()
{
    if (!m_updateChecker) return;
    postNotice(tr("Checking for updates…"), 2500);
    m_updateChecker->check();
}

QString PhoneController::latestUpdateVersion() const
{
    return m_latestUpdateVersion;
}

QString PhoneController::latestUpdateUrl() const
{
    return m_latestUpdateUrl.toString();
}

void PhoneController::openLatestUpdateUrl()
{
    const QString scheme = m_latestUpdateUrl.scheme().toLower();
    const bool canOpen =
        m_latestUpdateUrl.isValid()
        && !m_latestUpdateUrl.isEmpty()
        && !m_latestUpdateUrl.host().isEmpty()
        && (scheme == QLatin1String("https")
            || scheme == QLatin1String("http"));
    if (!canOpen) {
        postNotice(tr("No update download available"), 3000);
        return;
    }
    if (!QDesktopServices::openUrl(m_latestUpdateUrl)) {
        postNotice(tr("Could not open the update download"), 5000);
    }
}

ProvisioningController *PhoneController::provisioningController() const
{
    return m_provisioningController.get();
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

void PhoneController::redialFromHistory(int historyId)
{
    auto h = m_historyMgr->findById(static_cast<sip::HistoryId>(historyId));
    if (!h) return;
    const QString uri = QString::fromStdString(h->remoteUri);
    setDialerUri(uri);
    dial(uri);
}

int PhoneController::registeredAccountCount() const
{
    return m_accountsController ? m_accountsController->registeredAccountCount() : 0;
}

void PhoneController::requestQuit()
{
    QCoreApplication::quit();
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

} // namespace compactphone

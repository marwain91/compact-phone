#include "core/AccountsController.h"
#include "core/AccountsManager.h"
#include "core/BootConfig.h"
#include "core/CallManager.h"
#include "core/CoreSipGraph.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "core/sipbackend/ListenerFanout.h"
#include "core/sipbackend/pjsip/PjsipBackend.h"
#include "models/AccountsModel.h"
#include "persistence/Database.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QMetaObject>
#include <QTimer>

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>

#include <memory>
#include <utility>

namespace {

void applyLogLevel(const QString &lvl)
{
    if (lvl == QStringLiteral("trace")) spdlog::set_level(spdlog::level::trace);
    else if (lvl == QStringLiteral("debug")) spdlog::set_level(spdlog::level::debug);
    else if (lvl == QStringLiteral("warn")) spdlog::set_level(spdlog::level::warn);
    else if (lvl == QStringLiteral("error")) spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);
}

bool addLogFileSink(const QString &path)
{
    if (path.trimmed().isEmpty()) return false;
    try {
        auto sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(
            path.toStdString(), true);
        sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");
        if (auto logger = spdlog::default_logger()) {
            logger->sinks().push_back(std::move(sink));
            return true;
        }
    } catch (const spdlog::spdlog_ex &e) {
        spdlog::error("headless: cannot open log file {}: {}",
                      path.toStdString(), e.what());
    }
    return false;
}

class HeadlessRunner : public QObject {
public:
    explicit HeadlessRunner(compactphone::BootConfig cfg,
                            QObject *parent = nullptr)
        : QObject(parent), m_cfg(std::move(cfg))
    {
    }

    ~HeadlessRunner() override
    {
        // Quiesce the backend listener BEFORE managers die so no queued
        // events are delivered to AccountsManager during teardown.
        // (See CoreSipGraph.h wiring contract.)
        if (m_backend) m_backend->setListener(nullptr);
        // m_listener, m_accounts, m_calls, m_accountsController,
        // m_accountsModel, m_backend all destruct via unique_ptr in reverse
        // declaration order after this function returns (the fanout's sinks
        // are already severed by setListener(nullptr) above, so its order
        // relative to the managers is harmless). m_engine.stop() is not called
        // explicitly — the engine stays alive while pj::Account destructors
        // run inside m_accounts (via AccountsManager::~AccountsManager /
        // PjsipBackend::removeAccount). The engine stops when m_engine goes
        // out of scope or the process exits.
    }

    int start()
    {
        if (m_cfg.accounts.isEmpty()) {
            spdlog::error("headless: at least one account is required");
            return 2;
        }
        if (m_cfg.headlessPlayFile
            && !QFileInfo::exists(*m_cfg.headlessPlayFile)) {
            spdlog::error("headless: play file does not exist: {}",
                          m_cfg.headlessPlayFile->toStdString());
            return 2;
        }
        if (!m_db.openInMemory()) {
            spdlog::error("headless: database initialization failed");
            return 2;
        }
        if (!m_engine.start(0)) {
            spdlog::error("headless: SIP engine failed to start");
            return 2;
        }
        // PjsipBackend borrows the already-started SipEngine.
        m_backend = std::make_unique<compactphone::sipbackend::PjsipBackend>(&m_engine);

        auto core = compactphone::buildCoreSipGraph(
            m_backend.get(), &m_db, &m_keychain);
        m_accounts = std::move(core.accounts);
        m_accountsModel = std::move(core.accountsModel);
        m_accountsController = std::move(core.accountsController);
        m_calls = std::move(core.calls);
        m_listener = std::move(core.listener);

        m_autoAnswer =
            m_cfg.headlessAutoAnswer.value_or(m_cfg.autoAnswer.value_or(false));
        m_loopPlayFile = m_cfg.headlessLoopPlayFile.value_or(false);
        m_exitAfterCall = m_cfg.headlessExitAfterCall.value_or(false);

        wireCallbacks();
        applyAccounts();

        if (m_cfg.headlessDurationSec && *m_cfg.headlessDurationSec > 0) {
            QTimer::singleShot(*m_cfg.headlessDurationSec * 1000, this, [this] {
                spdlog::info("headless: duration elapsed");
                QCoreApplication::exit(0);
            });
        }

        if (m_cfg.headlessCallUri) {
            QTimer::singleShot(30000, this, [this] {
                if (!m_outboundPlaced && m_activeCallId == compactphone::sip::kInvalidCallId) {
                    spdlog::error("headless: no account registered before call timeout");
                    QCoreApplication::exit(3);
                }
            });
        } else if (!m_autoAnswer) {
            spdlog::info("headless: registered accounts only; waiting until quit");
        }

        return 0;
    }

private:
    compactphone::BootConfig m_cfg;
    compactphone::persistence::Database m_db;
    compactphone::platform::MemoryKeychain m_keychain;
    compactphone::sip::SipEngine m_engine;
    // PjsipBackend borrows m_engine; declared after engine so it is destroyed
    // first (unique_ptr destructors in reverse order). Accounts are removed
    // from the backend inside AccountsManager's destructor before m_backend
    // is reset. The destructor above handles the explicit teardown ordering.
    std::unique_ptr<compactphone::sipbackend::PjsipBackend> m_backend;
    // Routes backend events to accounts then calls; kept alive for the
    // backend's listener pointer, quiesced via setListener(nullptr) in dtor.
    std::unique_ptr<compactphone::sipbackend::ListenerFanout> m_listener;
    std::unique_ptr<compactphone::sip::AccountsManager> m_accounts;
    std::unique_ptr<compactphone::models::AccountsModel> m_accountsModel;
    std::unique_ptr<compactphone::AccountsController> m_accountsController;
    std::unique_ptr<compactphone::sip::CallManager> m_calls;
    compactphone::sip::CallId m_activeCallId = compactphone::sip::kInvalidCallId;
    bool m_autoAnswer = false;
    bool m_loopPlayFile = false;
    bool m_exitAfterCall = false;
    bool m_outboundPlaced = false;
    bool m_sawCall = false;

    void wireCallbacks()
    {
        QObject::connect(m_accounts.get(),
                         &compactphone::sip::AccountsManager::registrationStateChanged,
                         this, [this](compactphone::sip::AccountId id,
                                      compactphone::sip::RegistrationState state) {
                QMetaObject::invokeMethod(this, [this, id, state] {
                    onRegistrationState(id, state);
                }, Qt::QueuedConnection);
            });

        // The adapter wraps incoming calls eagerly inside the PJSIP callback;
        // CallManager records them and emits incomingCall on the main thread.
        QObject::connect(m_calls.get(),
                         &compactphone::sip::CallManager::incomingCall,
                         this, [this](int callId) { onIncomingCall(callId); });

        QObject::connect(m_calls.get(),
                         &compactphone::sip::CallManager::callEvent,
                         this, [this](compactphone::sip::CallId id,
                                      compactphone::sip::CallState state) {
                QMetaObject::invokeMethod(this, [this, id, state] {
                    onCallEvent(id, state);
                }, Qt::QueuedConnection);
            });
    }

    void applyAccounts()
    {
        for (const auto &account : m_cfg.accounts) {
            int existingId = compactphone::sip::kInvalidAccountId;
            if (m_cfg.replaceAccounts) {
                existingId = findExistingAccount(account);
            }

            QVariantMap params = account.params;
            if (!account.password.isEmpty()) params["password"] = account.password;

            if (existingId != compactphone::sip::kInvalidAccountId) {
                m_accountsController->updateAccount(existingId, params);
                spdlog::info("headless: updated account #{}", existingId);
            } else {
                const int id = m_accountsController->addAccount(params);
                spdlog::info("headless: created account #{}", id);
            }
        }
    }

    int findExistingAccount(const compactphone::BootAccount &account) const
    {
        const QString user = account.params.value("username").toString();
        const QString domain = account.params.value("domain").toString();
        if (user.isEmpty() || domain.isEmpty()) {
            return compactphone::sip::kInvalidAccountId;
        }

        for (const auto &existing : m_accounts->list()) {
            if (QString::fromStdString(existing.username) == user
                && QString::fromStdString(existing.domain) == domain) {
                return static_cast<int>(existing.id);
            }
        }
        return compactphone::sip::kInvalidAccountId;
    }

    void onRegistrationState(compactphone::sip::AccountId id,
                             compactphone::sip::RegistrationState state)
    {
        if (state == compactphone::sip::RegistrationState::Failed) {
            const auto err = m_accounts->lastRegErrorOf(id);
            spdlog::warn("headless: account {} registration failed: {} {}",
                         id, err.code, err.reason);
            return;
        }

        if (state != compactphone::sip::RegistrationState::Registered) return;
        spdlog::info("headless: account {} registered", id);

        if (!m_cfg.headlessCallUri || m_outboundPlaced) return;
        m_outboundPlaced = true;

        auto accountId = m_accounts->defaultAccountId();
        if (accountId == compactphone::sip::kInvalidAccountId
            || m_accounts->stateOf(accountId)
                   != compactphone::sip::RegistrationState::Registered) {
            accountId = id;
        }

        m_activeCallId = m_calls->makeCall(
            accountId, m_cfg.headlessCallUri->toStdString());
        if (m_activeCallId == compactphone::sip::kInvalidCallId) {
            spdlog::error("headless: failed to place call to {}",
                          m_cfg.headlessCallUri->toStdString());
            QCoreApplication::exit(3);
            return;
        }
        m_sawCall = true;
        spdlog::info("headless: calling {}", m_cfg.headlessCallUri->toStdString());
    }

    void onIncomingCall(int callId)
    {
        // The backend wrapped the call eagerly and CallManager recorded it
        // before emitting this signal — accept() is valid immediately.
        const auto id = static_cast<compactphone::sip::CallId>(callId);
        m_activeCallId = id;
        m_sawCall = true;
        spdlog::info("headless: incoming call {}", id);
        if (m_autoAnswer) {
            m_calls->accept(id);
        }
    }

    void onCallEvent(compactphone::sip::CallId id,
                     compactphone::sip::CallState state)
    {
        if (state == compactphone::sip::CallState::Confirmed) {
            m_activeCallId = id;
            m_sawCall = true;
            startPlaybackWhenReady(id, 15);
            return;
        }

        if (state == compactphone::sip::CallState::Disconnected) {
            if (id == m_activeCallId) {
                m_activeCallId = compactphone::sip::kInvalidCallId;
            }
            if (m_exitAfterCall && m_sawCall) {
                spdlog::info("headless: call disconnected");
                QCoreApplication::exit(0);
            }
        }
    }

    void startPlaybackWhenReady(compactphone::sip::CallId id, int retries)
    {
        if (!m_cfg.headlessPlayFile || m_cfg.headlessPlayFile->isEmpty()) return;
        if (!m_calls || m_calls->isPlayingAudioFile(id)) return;
        if (m_calls->playAudioFile(id, m_cfg.headlessPlayFile->toStdString(),
                                   m_loopPlayFile)) {
            return;
        }
        if (retries <= 0) {
            spdlog::warn("headless: media never became ready for playback");
            return;
        }
        QTimer::singleShot(400, this, [this, id, retries] {
            startPlaybackWhenReady(id, retries - 1);
        });
    }
};

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("Compact Phone Headless"));
    QCoreApplication::setOrganizationName(QStringLiteral("Havliczech"));
    QCoreApplication::setOrganizationDomain(QStringLiteral("havliczech.eu"));
    QCoreApplication::setApplicationVersion(QStringLiteral(COMPACTPHONE_VERSION));

    const auto cfg = compactphone::bootconfig::parseCommandLine(app.arguments());
    if (cfg.logLevel) applyLogLevel(*cfg.logLevel);
    if (cfg.logFile) addLogFileSink(*cfg.logFile);

    HeadlessRunner runner(cfg);
    const int startCode = runner.start();
    if (startCode != 0) return startCode;
    return app.exec();
}

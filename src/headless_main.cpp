#include "core/AccountsController.h"
#include "core/AccountsManager.h"
#include "core/BootConfig.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
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

        m_accounts = std::make_unique<compactphone::sip::AccountsManager>(
            &m_engine, &m_db, &m_keychain);
        m_accountsModel =
            std::make_unique<compactphone::models::AccountsModel>(m_accounts.get());
        m_accountsController = std::make_unique<compactphone::AccountsController>(
            m_accounts.get(), m_accountsModel.get(), &m_engine);
        m_calls = std::make_unique<compactphone::sip::CallManager>(
            m_accounts.get());

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
        m_accounts->setOnRegistrationStateChanged(
            [this](compactphone::sip::AccountId id,
                   compactphone::sip::RegistrationState state) {
                QMetaObject::invokeMethod(this, [this, id, state] {
                    onRegistrationState(id, state);
                }, Qt::QueuedConnection);
            });

        m_accounts->setOnIncomingCall(
            [this](compactphone::sip::AccountId accountId, int pjsipCallId) {
                QMetaObject::invokeMethod(this, [this, accountId, pjsipCallId] {
                    onIncomingCall(accountId, pjsipCallId);
                }, Qt::QueuedConnection);
            });

        m_calls->setOnCallEvent(
            [this](compactphone::sip::CallId id,
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

    void onIncomingCall(compactphone::sip::AccountId accountId, int pjsipCallId)
    {
        const auto id = m_calls->adoptIncomingCall(accountId, pjsipCallId);
        if (id == compactphone::sip::kInvalidCallId) {
            spdlog::warn("headless: incoming call could not be adopted");
            return;
        }
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

#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQuickStyle>
#include <QStandardPaths>
#include <QTranslator>

#if COMPACTPHONE_WITH_TRAY
#include <QApplication>
using CompactPhoneApplication = QApplication;
#else
using CompactPhoneApplication = QGuiApplication;
#endif

#include "core/BootConfig.h"
#include "core/CrashReporting.h"
#include "core/LogBuffer.h"
#include "core/PhoneController.h"
#include "core/UrlDispatcher.h"
#include "models/AccountsModel.h"

#include <spdlog/spdlog.h>

namespace {

// Applies a parsed BootConfig to the live PhoneController singleton. Lives in
// the executable target rather than compactphone_core so it can include
// PhoneController.h (which pulls in QML headers).
void applyBootConfig(const compactphone::BootConfig &cfg,
                     compactphone::PhoneController &controller)
{
    using compactphone::models::AccountsModel;

    if (cfg.logLevel) controller.setLogLevel(*cfg.logLevel);

    for (const auto &a : cfg.accounts) {
        int existingId = -1;
        if (cfg.replaceAccounts) {
            const QString user = a.params.value("username").toString();
            const QString domain = a.params.value("domain").toString();
            if (auto *model = controller.accountsModel();
                model && !user.isEmpty() && !domain.isEmpty()) {
                for (int i = 0; i < model->rowCount(); ++i) {
                    const int id = model->data(model->index(i, 0),
                                               AccountsModel::IdRole).toInt();
                    const auto snap = controller.accountSnapshot(id);
                    if (snap.value("username").toString() == user
                        && snap.value("domain").toString() == domain) {
                        existingId = id;
                        break;
                    }
                }
            }
        }

        QVariantMap params = a.params;
        if (!a.password.isEmpty()) params["password"] = a.password;

        if (existingId > 0) {
            controller.updateAccount(existingId, params);
            spdlog::info("BootConfig: updated account #{} ({})",
                         existingId,
                         params.value("label").toString().toStdString());
        } else {
            const int id = controller.addAccount(params);
            spdlog::info("BootConfig: created account #{} ({})",
                         id,
                         params.value("label").toString().toStdString());
        }
    }

    if (cfg.autoAnswer) controller.setAutoAnswerEnabled(*cfg.autoAnswer);
    if (cfg.dnd)        controller.setDndEnabled(*cfg.dnd);
    if (cfg.theme)      controller.setThemeId(*cfg.theme);
}

} // namespace

int main(int argc, char *argv[])
{
    CompactPhoneApplication app(argc, argv);
    QCoreApplication::setApplicationName("Compact Phone");
    // Display name drives the dock tooltip, About box, and window titles in
    // some Qt versions — keep it in sync with the CFBundleName / .desktop Name.
    QGuiApplication::setApplicationDisplayName("Compact Phone");
    QCoreApplication::setOrganizationName("Havliczech");
    QCoreApplication::setOrganizationDomain("havliczech.eu");
    QCoreApplication::setApplicationVersion(QStringLiteral(COMPACTPHONE_VERSION));
    QGuiApplication::setQuitOnLastWindowClosed(false);

    // Initialize the in-memory log tail before any other component logs, so
    // the diagnostics export and log viewer capture everything from boot.
    compactphone::LogBuffer::instance();

    // Load the .qm matching the system locale. The .qm files are embedded
    // under :/i18n/ by qt_add_translations() in src/CMakeLists.txt.
    static QTranslator translator;
    if (translator.load(QLocale(), QStringLiteral("compactphone"),
                        QStringLiteral("_"), QStringLiteral(":/i18n"))) {
        QCoreApplication::installTranslator(&translator);
    }

    // Boot-time provisioning: --sip-server X --sip-user Y --sip-password Z …
    // Run before any SIP/UI work so a clean install can register immediately.
    const auto bootCfg =
        compactphone::bootconfig::parseCommandLine(app.arguments());

    // Install the sip:/tel:/callto: URL handler as early as possible so
    // launches triggered by clicking a SIP link don't drop the URL.
    app.installEventFilter(compactphone::UrlDispatcher::instance());
    // Dock / taskbar icon. macOS will rasterize the SVG at the dock size
    // automatically; we provide a single resolution-independent source.
    QGuiApplication::setWindowIcon(QIcon(QStringLiteral(":/branding/appicon.svg")));
    QQuickStyle::setStyle("Basic");

    // Extract bundled ringtone to AppDataLocation on first run so PJSIP can
    // read it from a real filesystem path.
    const QString appDataDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(appDataDir);
    const QString ringtoneDst = appDataDir + QStringLiteral("/ringtone.wav");
    if (!QFile::exists(ringtoneDst)) {
        QFile src(QStringLiteral(":/resources/ringtone.wav"));
        if (src.open(QIODevice::ReadOnly)) {
            QFile out(ringtoneDst);
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                out.write(src.readAll());
            }
        }
    }

    QQmlApplicationEngine engine;
    QObject::connect(
        &engine,
        &QQmlApplicationEngine::objectCreationFailed,
        &app,
        []() { QCoreApplication::exit(-1); },
        Qt::QueuedConnection
    );

    engine.loadFromModule("CompactPhone", "Main");

    auto *pc = engine.singletonInstance<compactphone::PhoneController *>(
        "CompactPhone", "PhoneController");

    if (pc && !bootCfg.empty()) {
        // QML evaluation has already instantiated the PhoneController
        // singleton via Main.qml bindings; apply the boot config now that
        // every sub-controller is wired up.
        applyBootConfig(bootCfg, *pc);
    }

    // Crash reporting — only honored when the build was configured with
    // -DCOMPACTPHONE_ENABLE_SENTRY=ON AND the user has opted in via
    // Settings → Advanced. Both gates are checked inside initSentry.
#if defined(COMPACTPHONE_SENTRY_DSN)
    if (pc) {
        compactphone::crash::initSentry(
            QStringLiteral(COMPACTPHONE_SENTRY_DSN),
            pc->crashReportingEnabled());
    }
#endif

    const int rc = app.exec();
    compactphone::crash::shutdown();
    return rc;
}

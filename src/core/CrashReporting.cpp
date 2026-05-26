#include "CrashReporting.h"

#include <QUrl>

#include <spdlog/spdlog.h>

// Sentry headers are only present when COMPACTPHONE_ENABLE_SENTRY=ON is
// passed to CMake AND vcpkg has the `sentry-native` port installed.
#if defined(COMPACTPHONE_ENABLE_SENTRY)
#include <sentry.h>
#endif

namespace compactphone::crash {

namespace {

QString configuredDsn()
{
#if defined(COMPACTPHONE_SENTRY_DSN)
    return QStringLiteral(COMPACTPHONE_SENTRY_DSN);
#else
    return {};
#endif
}

} // namespace

bool isValidSentryDsn(const QString &dsn)
{
    const QUrl url(dsn, QUrl::StrictMode);
    return url.isValid()
        && (url.scheme() == QStringLiteral("https")
            || url.scheme() == QStringLiteral("http"))
        && !url.host().isEmpty();
}

bool configuredSentryAvailable()
{
#if defined(COMPACTPHONE_ENABLE_SENTRY)
    return isValidSentryDsn(configuredDsn());
#else
    return false;
#endif
}

void initConfiguredSentry(bool userConsent)
{
    initSentry(configuredDsn(), userConsent);
}

#if defined(COMPACTPHONE_ENABLE_SENTRY)

static bool s_initialized = false;

void initSentry(const QString &dsn, bool userConsent)
{
    if (s_initialized || !isValidSentryDsn(dsn) || !userConsent) return;

    sentry_options_t *opts = sentry_options_new();
    sentry_options_set_dsn(opts, dsn.toUtf8().constData());
    sentry_options_set_release(opts,
        ("compactphone@" COMPACTPHONE_VERSION));
    sentry_options_set_database_path(opts, ".sentry-native");
    sentry_options_set_auto_session_tracking(opts, 0);
    // Do not set user/context data. SIP credentials never go to crash reports.

    if (sentry_init(opts) == 0) {
        s_initialized = true;
        spdlog::info("Crash reporting: Sentry initialized");
    } else {
        spdlog::warn("Crash reporting: Sentry init failed");
    }
}

void shutdown()
{
    if (!s_initialized) return;
    sentry_close();
    s_initialized = false;
}

#else // !COMPACTPHONE_ENABLE_SENTRY

void initSentry(const QString &, bool) {}
void shutdown() {}

#endif

} // namespace compactphone::crash

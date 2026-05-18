#include "CrashReporting.h"

#include <spdlog/spdlog.h>

// Sentry headers are only present when COMPACTPHONE_ENABLE_SENTRY=ON is
// passed to CMake AND vcpkg has the `sentry-native` port installed.
#if defined(COMPACTPHONE_ENABLE_SENTRY)
#include <sentry.h>
#endif

namespace compactphone::crash {

#if defined(COMPACTPHONE_ENABLE_SENTRY)

static bool s_initialized = false;

void initSentry(const QString &dsn, bool userConsent)
{
    if (s_initialized || dsn.isEmpty() || !userConsent) return;

    sentry_options_t *opts = sentry_options_new();
    sentry_options_set_dsn(opts, dsn.toUtf8().constData());
    sentry_options_set_release(opts,
        ("compactphone@" COMPACTPHONE_VERSION));
    sentry_options_set_database_path(opts, ".sentry-native");
    sentry_options_set_auto_session_tracking(opts, 0);
    // Strip PII the SDK might otherwise grab. SIP credentials never go.
    sentry_options_set_send_default_pii(opts, 0);

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

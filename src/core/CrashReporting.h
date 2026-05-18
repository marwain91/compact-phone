#pragma once

#include <QString>

namespace compactphone::crash {

// Initialize the crash reporter (Sentry). No-op build unless COMPACTPHONE_ENABLE_SENTRY
// was set at configure time AND the user has opted in via Settings → Privacy.
//
// dsn: Sentry DSN URL (https://...ingest.sentry.io/...). The build embeds
// it at compile time from -DCOMPACTPHONE_SENTRY_DSN. Empty = disabled.
//
// userConsent: true once the user has explicitly opted in via Settings.
void initSentry(const QString &dsn, bool userConsent);

// Flush any pending events and shut down the reporter. Safe to call even
// if init was never called.
void shutdown();

} // namespace compactphone::crash

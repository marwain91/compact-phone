#include "core/CrashReporting.h"

#include <gtest/gtest.h>

namespace crash = compactphone::crash;

// Without COMPACTPHONE_ENABLE_SENTRY, both functions are no-ops by
// construction. These tests just confirm they don't crash under any
// argument combination — the build flag itself is the contract.

TEST(CrashReportingTest, InitWithoutDsnIsSafe)
{
    crash::initSentry(QString(), false);
    crash::initSentry(QString(), true);
    crash::shutdown();
}

TEST(CrashReportingTest, DsnValidationRejectsUnsafeOrMalformedValues)
{
    EXPECT_FALSE(crash::isValidSentryDsn(QString()));
    EXPECT_FALSE(crash::isValidSentryDsn(QStringLiteral("not a url")));
    EXPECT_FALSE(crash::isValidSentryDsn(QStringLiteral("file:///tmp/report")));
    EXPECT_FALSE(crash::isValidSentryDsn(QStringLiteral("https:///1")));
}

TEST(CrashReportingTest, DsnValidationAcceptsHttpsSentryLikeUrl)
{
    EXPECT_TRUE(crash::isValidSentryDsn(
        QStringLiteral("https://public@example.ingest.sentry.io/123")));
}

TEST(CrashReportingTest, ConfiguredInitAndAvailabilityAreSafeWithoutBuildDsn)
{
    EXPECT_FALSE(crash::configuredSentryAvailable());
    crash::initConfiguredSentry(false);
    crash::initConfiguredSentry(true);
    crash::shutdown();
}

TEST(CrashReportingTest, InitWithDsnButNoConsentIsNoOp)
{
    crash::initSentry(QStringLiteral("https://abc@sentry.example/1"), false);
    crash::shutdown();
    // Repeating is also safe.
    crash::shutdown();
}

TEST(CrashReportingTest, RepeatedInitIsIdempotent)
{
    crash::initSentry(QStringLiteral("https://abc@sentry.example/1"), true);
    crash::initSentry(QStringLiteral("https://abc@sentry.example/1"), true);
    crash::shutdown();
}

// Smoke tests for the stack-neutral SIP backend types: defaults and
// invalid-id constants. Real behavior is covered by the fake-backend and
// contract suites; this pins the value-type contracts the spec promises.
#include "core/sipbackend/Types.h"

#include <gtest/gtest.h>

using namespace compactphone::sipbackend;

TEST(SipBackendTypes, InvalidIdsAreNegative)
{
    EXPECT_LT(kInvalidAccountId, 0);
    EXPECT_LT(kInvalidCallId, 0);
    EXPECT_LT(kInvalidWatchId, 0);
}

TEST(SipBackendTypes, EngineConfigDefaultsToStandardSipPort)
{
    EngineConfig cfg;
    EXPECT_EQ(cfg.sipPort, 5060);
}

TEST(SipBackendTypes, StreamStatsDefaultsToUnpopulated)
{
    StreamStats s;
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST(SipBackendTypes, AccountSettingsDefaultsMatchAccountValueObject)
{
    AccountSettings a;
    EXPECT_EQ(a.transport, Transport::Udp);
    EXPECT_EQ(a.srtpMode, SrtpMode::Optional);
    EXPECT_EQ(a.dtmfMethod, DtmfMethod::Rfc2833);
    EXPECT_TRUE(a.sessionTimersEnabled);
    EXPECT_FALSE(a.iceEnabled);
    EXPECT_EQ(a.registerIntervalSec, 0);
}

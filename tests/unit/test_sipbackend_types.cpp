// Smoke tests for the stack-neutral SIP backend types: defaults and
// invalid-id constants. Real behavior is covered by the fake-backend and
// contract suites; this pins the value-type contracts the spec promises.
//
// The boundary headers (Types.h / ISipBackend.h) must stay free of pj::,
// Qt, and core/ includes — but this test file may include anything. We use
// that latitude to enforce the "mirrors X value-for-value" promises in
// Types.h with static_asserts: if any enumerator drifts, the build breaks
// at this file rather than silently in phase-2/4 mapping code.
#include "core/sipbackend/Types.h"

#include "core/Account.h"     // sip::Transport, SrtpMode, DtmfMethod; Account defaults
#include "core/CallManager.h" // sip::CallState
#include "core/WatchedLine.h" // sip::LineState

#include <gtest/gtest.h>

// ---------------------------------------------------------------------------
// Parity enforcement — static_asserts so drift breaks the BUILD, not a test
// ---------------------------------------------------------------------------

// sipbackend::CallState mirrors compactphone::sip::CallState value-for-value.
static_assert(static_cast<int>(compactphone::sipbackend::CallState::Idle)
                  == static_cast<int>(compactphone::sip::CallState::Idle),
              "sipbackend::CallState::Idle drifted from sip::CallState::Idle");
static_assert(static_cast<int>(compactphone::sipbackend::CallState::Calling)
                  == static_cast<int>(compactphone::sip::CallState::Calling),
              "sipbackend::CallState::Calling drifted from sip::CallState::Calling");
static_assert(static_cast<int>(compactphone::sipbackend::CallState::EarlyMedia)
                  == static_cast<int>(compactphone::sip::CallState::EarlyMedia),
              "sipbackend::CallState::EarlyMedia drifted from sip::CallState::EarlyMedia");
static_assert(static_cast<int>(compactphone::sipbackend::CallState::Confirmed)
                  == static_cast<int>(compactphone::sip::CallState::Confirmed),
              "sipbackend::CallState::Confirmed drifted from sip::CallState::Confirmed");
static_assert(static_cast<int>(compactphone::sipbackend::CallState::Disconnected)
                  == static_cast<int>(compactphone::sip::CallState::Disconnected),
              "sipbackend::CallState::Disconnected drifted from sip::CallState::Disconnected");

// sipbackend::Transport mirrors sip::Transport value-for-value.
static_assert(static_cast<int>(compactphone::sipbackend::Transport::Udp)
                  == static_cast<int>(compactphone::sip::Transport::Udp),
              "sipbackend::Transport::Udp drifted from sip::Transport::Udp");
static_assert(static_cast<int>(compactphone::sipbackend::Transport::Tcp)
                  == static_cast<int>(compactphone::sip::Transport::Tcp),
              "sipbackend::Transport::Tcp drifted from sip::Transport::Tcp");
static_assert(static_cast<int>(compactphone::sipbackend::Transport::Tls)
                  == static_cast<int>(compactphone::sip::Transport::Tls),
              "sipbackend::Transport::Tls drifted from sip::Transport::Tls");

// sipbackend::SrtpMode mirrors sip::SrtpMode value-for-value.
static_assert(static_cast<int>(compactphone::sipbackend::SrtpMode::Disabled)
                  == static_cast<int>(compactphone::sip::SrtpMode::Disabled),
              "sipbackend::SrtpMode::Disabled drifted from sip::SrtpMode::Disabled");
static_assert(static_cast<int>(compactphone::sipbackend::SrtpMode::Optional)
                  == static_cast<int>(compactphone::sip::SrtpMode::Optional),
              "sipbackend::SrtpMode::Optional drifted from sip::SrtpMode::Optional");
static_assert(static_cast<int>(compactphone::sipbackend::SrtpMode::Required)
                  == static_cast<int>(compactphone::sip::SrtpMode::Required),
              "sipbackend::SrtpMode::Required drifted from sip::SrtpMode::Required");

// sipbackend::DtmfMethod mirrors sip::DtmfMethod value-for-value.
static_assert(static_cast<int>(compactphone::sipbackend::DtmfMethod::Inband)
                  == static_cast<int>(compactphone::sip::DtmfMethod::Inband),
              "sipbackend::DtmfMethod::Inband drifted from sip::DtmfMethod::Inband");
static_assert(static_cast<int>(compactphone::sipbackend::DtmfMethod::Rfc2833)
                  == static_cast<int>(compactphone::sip::DtmfMethod::Rfc2833),
              "sipbackend::DtmfMethod::Rfc2833 drifted from sip::DtmfMethod::Rfc2833");
static_assert(static_cast<int>(compactphone::sipbackend::DtmfMethod::Info)
                  == static_cast<int>(compactphone::sip::DtmfMethod::Info),
              "sipbackend::DtmfMethod::Info drifted from sip::DtmfMethod::Info");

// sipbackend::PresenceState mirrors sip::LineState value-for-value.
static_assert(static_cast<int>(compactphone::sipbackend::PresenceState::Unknown)
                  == static_cast<int>(compactphone::sip::LineState::Unknown),
              "sipbackend::PresenceState::Unknown drifted from sip::LineState::Unknown");
static_assert(static_cast<int>(compactphone::sipbackend::PresenceState::Idle)
                  == static_cast<int>(compactphone::sip::LineState::Idle),
              "sipbackend::PresenceState::Idle drifted from sip::LineState::Idle");
static_assert(static_cast<int>(compactphone::sipbackend::PresenceState::Busy)
                  == static_cast<int>(compactphone::sip::LineState::Busy),
              "sipbackend::PresenceState::Busy drifted from sip::LineState::Busy");
static_assert(static_cast<int>(compactphone::sipbackend::PresenceState::Offline)
                  == static_cast<int>(compactphone::sip::LineState::Offline),
              "sipbackend::PresenceState::Offline drifted from sip::LineState::Offline");

// ---------------------------------------------------------------------------
// Runtime tests
// ---------------------------------------------------------------------------

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

TEST(SipBackendTypes, AudioDeviceDefaultsToUnpopulated)
{
    AudioDevice d;
    EXPECT_EQ(d.id, -1);
}

// Verify AccountSettings defaults match the canonical sip::Account defaults
// for the fields the two structs share. Enums are compared through int because
// sipbackend:: and sip:: define separate-but-mirrored types.
TEST(SipBackendTypes, AccountSettingsDefaultsMatchAccountValueObject)
{
    const AccountSettings a;
    const compactphone::sip::Account ref;

    EXPECT_EQ(static_cast<int>(a.transport),
              static_cast<int>(ref.transport));
    EXPECT_EQ(static_cast<int>(a.srtpMode),
              static_cast<int>(ref.srtpMode));
    EXPECT_EQ(static_cast<int>(a.dtmfMethod),
              static_cast<int>(ref.dtmfMethod));
    EXPECT_EQ(a.sessionTimersEnabled, ref.sessionTimersEnabled);
    EXPECT_EQ(a.publishPresenceEnabled, ref.publishPresenceEnabled);
    EXPECT_EQ(a.iceEnabled, ref.iceEnabled);
    EXPECT_EQ(a.hideCallerId, ref.hideCallerId);
    EXPECT_EQ(a.zrtpEnabled, ref.zrtpEnabled);
    EXPECT_EQ(a.allowUntrustedCert, ref.allowUntrustedCert);
    EXPECT_EQ(a.registerIntervalSec, ref.registerIntervalSec);
    EXPECT_EQ(a.keepaliveIntervalSec, ref.keepaliveIntervalSec);
}

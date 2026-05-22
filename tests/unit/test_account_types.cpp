#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/CallEntry.h"

TEST(AccountTypes, TransportRoundTripsKnownValuesAndDefaultsUnknownToUdp)
{
    EXPECT_STREQ(compactphone::sip::transportToString(compactphone::sip::Transport::Udp), "udp");
    EXPECT_STREQ(compactphone::sip::transportToString(compactphone::sip::Transport::Tcp), "tcp");
    EXPECT_STREQ(compactphone::sip::transportToString(compactphone::sip::Transport::Tls), "tls");

    EXPECT_EQ(compactphone::sip::transportFromString("udp"), compactphone::sip::Transport::Udp);
    EXPECT_EQ(compactphone::sip::transportFromString("tcp"), compactphone::sip::Transport::Tcp);
    EXPECT_EQ(compactphone::sip::transportFromString("tls"), compactphone::sip::Transport::Tls);
    EXPECT_EQ(compactphone::sip::transportFromString("bogus"), compactphone::sip::Transport::Udp);
}

TEST(AccountTypes, SrtpModeRoundTripsKnownValuesAndDefaultsUnknownToOptional)
{
    EXPECT_STREQ(compactphone::sip::srtpModeToString(compactphone::sip::SrtpMode::Disabled), "disabled");
    EXPECT_STREQ(compactphone::sip::srtpModeToString(compactphone::sip::SrtpMode::Optional), "optional");
    EXPECT_STREQ(compactphone::sip::srtpModeToString(compactphone::sip::SrtpMode::Required), "required");

    EXPECT_EQ(compactphone::sip::srtpModeFromString("disabled"),
              compactphone::sip::SrtpMode::Disabled);
    EXPECT_EQ(compactphone::sip::srtpModeFromString("optional"),
              compactphone::sip::SrtpMode::Optional);
    EXPECT_EQ(compactphone::sip::srtpModeFromString("required"),
              compactphone::sip::SrtpMode::Required);
    EXPECT_EQ(compactphone::sip::srtpModeFromString("mandatory"),
              compactphone::sip::SrtpMode::Required);
    EXPECT_EQ(compactphone::sip::srtpModeFromString("bogus"),
              compactphone::sip::SrtpMode::Optional);
}

TEST(AccountTypes, DtmfMethodRoundTripsKnownValuesAndDefaultsUnknownToRfc2833)
{
    EXPECT_STREQ(compactphone::sip::dtmfMethodToString(compactphone::sip::DtmfMethod::Inband), "inband");
    EXPECT_STREQ(compactphone::sip::dtmfMethodToString(compactphone::sip::DtmfMethod::Rfc2833), "rfc2833");
    EXPECT_STREQ(compactphone::sip::dtmfMethodToString(compactphone::sip::DtmfMethod::Info), "info");

    EXPECT_EQ(compactphone::sip::dtmfMethodFromString("inband"),
              compactphone::sip::DtmfMethod::Inband);
    EXPECT_EQ(compactphone::sip::dtmfMethodFromString("rfc2833"),
              compactphone::sip::DtmfMethod::Rfc2833);
    EXPECT_EQ(compactphone::sip::dtmfMethodFromString("info"),
              compactphone::sip::DtmfMethod::Info);
    EXPECT_EQ(compactphone::sip::dtmfMethodFromString("bogus"),
              compactphone::sip::DtmfMethod::Rfc2833);
}

TEST(AccountTypes, CallDirectionAndStateStringsMatchQmlContract)
{
    EXPECT_STREQ(compactphone::sip::callDirectionToString(compactphone::sip::CallDirection::Inbound), "in");
    EXPECT_STREQ(compactphone::sip::callDirectionToString(compactphone::sip::CallDirection::Outbound), "out");

    EXPECT_STREQ(compactphone::sip::callStateToCString(compactphone::sip::CallState::Idle), "idle");
    EXPECT_STREQ(compactphone::sip::callStateToCString(compactphone::sip::CallState::Calling), "calling");
    EXPECT_STREQ(compactphone::sip::callStateToCString(compactphone::sip::CallState::EarlyMedia), "early");
    EXPECT_STREQ(compactphone::sip::callStateToCString(compactphone::sip::CallState::Confirmed), "active");
    EXPECT_STREQ(compactphone::sip::callStateToCString(compactphone::sip::CallState::Disconnected), "disconnected");
}

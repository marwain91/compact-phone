#include "core/CallManager.h"
#include "core/sipbackend/fake/FakeSipBackend.h"

#include <gtest/gtest.h>

namespace sip = compactphone::sip;
namespace sb = compactphone::sipbackend;

// CallManager delegates streamStats to the backend; an unknown/invalid call
// id yields the fake's defaulted (all -1) StreamStats. No AccountsManager is
// needed for these reads, so it is passed as nullptr.

TEST(StreamStatsTest, UnknownCallIdReturnsAllNegativeOne)
{
    sb::FakeSipBackend fake;
    sip::CallManager cm(&fake, /*accounts=*/nullptr);
    const auto s = cm.streamStats(9999);
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST(StreamStatsTest, InvalidCallIdReturnsAllNegativeOne)
{
    sb::FakeSipBackend fake;
    sip::CallManager cm(&fake, /*accounts=*/nullptr);
    const auto s = cm.streamStats(sip::kInvalidCallId);
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST(StreamStatsTest, NegativeCallIdDoesNotCrash)
{
    sb::FakeSipBackend fake;
    sip::CallManager cm(&fake, /*accounts=*/nullptr);
    const auto s = cm.streamStats(-42);
    // No specific value — just that we didn't crash and got defaults.
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
}

#include "core/CallManager.h"

#include <gtest/gtest.h>

namespace sip = compactphone::sip;

TEST(StreamStatsTest, UnknownCallIdReturnsAllNegativeOne)
{
    sip::CallManager cm(/*accounts=*/nullptr);
    const auto s = cm.streamStats(9999);
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST(StreamStatsTest, InvalidCallIdReturnsAllNegativeOne)
{
    sip::CallManager cm(/*accounts=*/nullptr);
    const auto s = cm.streamStats(sip::kInvalidCallId);
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST(StreamStatsTest, NegativeCallIdDoesNotCrash)
{
    sip::CallManager cm(/*accounts=*/nullptr);
    const auto s = cm.streamStats(-42);
    // No specific value — just that we didn't crash and got defaults.
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
}

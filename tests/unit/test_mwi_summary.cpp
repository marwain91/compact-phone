// Tests for parseMwiSummary — moved from AccountImpl::onMwiInfo.
// Every case is pure (no SIP stack, no QApplication needed).

#include "core/sipbackend/pjsip/MwiSummary.h"

#include <gtest/gtest.h>

using compactphone::sipbackend::MwiSummary;
using compactphone::sipbackend::parseMwiSummary;

TEST(MwiSummaryTest, VoiceMessage_NewAndOld)
{
    const MwiSummary s = parseMwiSummary("Voice-Message: 2/5");
    EXPECT_EQ(s.newMessages, 2);
    EXPECT_EQ(s.oldMessages, 5);
    EXPECT_TRUE(s.active);
}

TEST(MwiSummaryTest, VoiceMessage_ZeroNew)
{
    const MwiSummary s = parseMwiSummary("Voice-Message: 0/3");
    EXPECT_EQ(s.newMessages, 0);
    EXPECT_EQ(s.oldMessages, 3);
    EXPECT_FALSE(s.active);
}

TEST(MwiSummaryTest, MessagesWaiting_Yes_OnlyActive)
{
    const MwiSummary s = parseMwiSummary("Messages-Waiting: yes");
    EXPECT_EQ(s.newMessages, 0);
    EXPECT_EQ(s.oldMessages, 0);
    EXPECT_TRUE(s.active);
}

TEST(MwiSummaryTest, MessagesWaiting_No)
{
    const MwiSummary s = parseMwiSummary("Messages-Waiting: no");
    EXPECT_EQ(s.newMessages, 0);
    EXPECT_EQ(s.oldMessages, 0);
    EXPECT_FALSE(s.active);
}

TEST(MwiSummaryTest, EmptyBody)
{
    const MwiSummary s = parseMwiSummary("");
    EXPECT_EQ(s.newMessages, 0);
    EXPECT_EQ(s.oldMessages, 0);
    EXPECT_FALSE(s.active);
}

TEST(MwiSummaryTest, VoiceMessage_WhitespaceAfterColon)
{
    // Tab + space after colon should still parse correctly
    const MwiSummary s = parseMwiSummary("Voice-Message:\t 4/1");
    EXPECT_EQ(s.newMessages, 4);
    EXPECT_EQ(s.oldMessages, 1);
    EXPECT_TRUE(s.active);
}

TEST(MwiSummaryTest, RealisticNotifyBody)
{
    // Full SIMPLE message-summary NOTIFY body with CRLF lines and urgent counts
    const std::string body =
        "Messages-Waiting: yes\r\n"
        "Message-Account: sip:alice@example.com\r\n"
        "Voice-Message: 1/0 (0/0)\r\n";
    const MwiSummary s = parseMwiSummary(body);
    EXPECT_EQ(s.newMessages, 1);
    EXPECT_EQ(s.oldMessages, 0);
    EXPECT_TRUE(s.active);
}

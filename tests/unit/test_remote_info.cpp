// Tests for parseRemoteInfo — extracted from announceIncomingCall.
// Every case is pure (no SIP stack, no QApplication needed).

#include "core/sipbackend/pjsip/RemoteInfo.h"

#include <gtest/gtest.h>

using compactphone::sipbackend::RemoteInfo;
using compactphone::sipbackend::parseRemoteInfo;

TEST(RemoteInfoTest, QuotedDisplayName)
{
    const RemoteInfo ri = parseRemoteInfo(R"("Alice" <sip:a@h>)");
    EXPECT_EQ(ri.uri, "sip:a@h");
    EXPECT_EQ(ri.displayName, "Alice");
}

TEST(RemoteInfoTest, UnquotedDisplayName)
{
    const RemoteInfo ri = parseRemoteInfo("Alice <sip:a@h>");
    EXPECT_EQ(ri.uri, "sip:a@h");
    EXPECT_EQ(ri.displayName, "Alice");
}

TEST(RemoteInfoTest, AngleBracketsOnly)
{
    const RemoteInfo ri = parseRemoteInfo("<sip:a@h>");
    EXPECT_EQ(ri.uri, "sip:a@h");
    EXPECT_EQ(ri.displayName, "");
}

TEST(RemoteInfoTest, BareUri)
{
    const RemoteInfo ri = parseRemoteInfo("sip:a@h");
    EXPECT_EQ(ri.uri, "sip:a@h");
    EXPECT_EQ(ri.displayName, "");
}

TEST(RemoteInfoTest, MultiWordDisplayName)
{
    const RemoteInfo ri = parseRemoteInfo(R"("John Smith" <sip:j@h>)");
    EXPECT_EQ(ri.uri, "sip:j@h");
    EXPECT_EQ(ri.displayName, "John Smith");
}

TEST(RemoteInfoTest, EmptyString)
{
    const RemoteInfo ri = parseRemoteInfo("");
    EXPECT_EQ(ri.uri, "");
    EXPECT_EQ(ri.displayName, "");
}

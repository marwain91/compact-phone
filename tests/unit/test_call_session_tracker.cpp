#include <gtest/gtest.h>

#include "core/CallSessionTracker.h"

namespace {

compactphone::sip::CallEntry entry(compactphone::sip::CallId id,
                              compactphone::sip::AccountId accountId,
                              std::string remoteUri,
                              compactphone::sip::CallDirection direction)
{
    compactphone::sip::CallEntry e;
    e.id = id;
    e.accountId = accountId;
    e.remoteUri = std::move(remoteUri);
    e.direction = direction;
    return e;
}

} // namespace

TEST(CallSessionTracker, TracksOverlappingCallsIndependently)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto first = entry(1, 10, "sip:first@example.com",
                             compactphone::sip::CallDirection::Outbound);
    const auto second = entry(2, 20, "sip:second@example.com",
                              compactphone::sip::CallDirection::Outbound);

    tracker.noteOutbound(first.id, first.accountId, first.remoteUri, 1000);
    EXPECT_FALSE(tracker.noteState(first, compactphone::sip::CallState::Confirmed, 2000));

    tracker.noteOutbound(second.id, second.accountId, second.remoteUri, 3000);
    EXPECT_FALSE(tracker.noteState(second, compactphone::sip::CallState::Confirmed, 4000));

    const auto firstHistory =
        tracker.noteState(first, compactphone::sip::CallState::Disconnected, 7000);
    ASSERT_TRUE(firstHistory.has_value());
    EXPECT_EQ(firstHistory->accountId, 10);
    EXPECT_EQ(firstHistory->remoteUri, "sip:first@example.com");
    EXPECT_EQ(firstHistory->durationMs, 5000);

    const auto secondHistory =
        tracker.noteState(second, compactphone::sip::CallState::Disconnected, 9000);
    ASSERT_TRUE(secondHistory.has_value());
    EXPECT_EQ(secondHistory->accountId, 20);
    EXPECT_EQ(secondHistory->remoteUri, "sip:second@example.com");
    EXPECT_EQ(secondHistory->durationMs, 5000);
}

TEST(CallSessionTracker, LogsMissedInboundCall)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto inbound = entry(7, 30, "sip:caller@example.com",
                               compactphone::sip::CallDirection::Inbound);

    tracker.noteIncoming(inbound, 100);
    const auto history =
        tracker.noteState(inbound, compactphone::sip::CallState::Disconnected, 500);

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->accountId, 30);
    EXPECT_EQ(history->direction, compactphone::sip::CallDirection::Inbound);
    EXPECT_EQ(history->remoteUri, "sip:caller@example.com");
    EXPECT_EQ(history->startedAt, 100);
    EXPECT_EQ(history->durationMs, 0);
}

TEST(CallSessionTracker, PreservesDirectionWhenDisconnectedSnapshotIsGone)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto inbound = entry(8, 40, "sip:late@example.com",
                               compactphone::sip::CallDirection::Inbound);
    compactphone::sip::CallEntry disconnected;
    disconnected.id = inbound.id;

    tracker.noteIncoming(inbound, 100);
    EXPECT_FALSE(tracker.noteState(inbound,
                                   compactphone::sip::CallState::Confirmed,
                                   200));
    const auto history =
        tracker.noteState(disconnected, compactphone::sip::CallState::Disconnected, 700);

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->direction, compactphone::sip::CallDirection::Inbound);
    EXPECT_EQ(history->accountId, 40);
    EXPECT_EQ(history->remoteUri, "sip:late@example.com");
    EXPECT_EQ(history->durationMs, 500);
}

TEST(CallSessionTracker, LogsOutboundThatNeverConnects)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto out = entry(11, 50, "sip:nooneanswers@example.com",
                           compactphone::sip::CallDirection::Outbound);

    tracker.noteOutbound(out.id, out.accountId, out.remoteUri, 1000);
    // Caller hangs up before remote picks up — straight to Disconnected.
    const auto history =
        tracker.noteState(out, compactphone::sip::CallState::Disconnected, 3500);

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->direction, compactphone::sip::CallDirection::Outbound);
    EXPECT_EQ(history->accountId, 50);
    EXPECT_EQ(history->remoteUri, "sip:nooneanswers@example.com");
    // No Confirmed ever — durationMs is 0; startedAt falls back to firstSeenAt.
    EXPECT_EQ(history->startedAt, 1000);
    EXPECT_EQ(history->durationMs, 0);
}

TEST(CallSessionTracker, SwallowsDisconnectWithNoPriorTrackingAndNoEntryDetails)
{
    compactphone::sip::CallSessionTracker tracker;
    // Tracker never saw this call; PJSIP sends a bare disconnect with no
    // accountId / remoteUri. The tracker has nothing to log → nullopt.
    compactphone::sip::CallEntry bare;
    bare.id = 99;
    EXPECT_FALSE(tracker.noteState(
        bare, compactphone::sip::CallState::Disconnected, 1000));
}

TEST(CallSessionTracker, IgnoresDuplicateConfirmedForStartedAt)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto e = entry(12, 60, "sip:reinvite@example.com",
                         compactphone::sip::CallDirection::Outbound);

    tracker.noteOutbound(e.id, e.accountId, e.remoteUri, 100);
    tracker.noteState(e, compactphone::sip::CallState::Confirmed, 500);
    // A re-INVITE could re-emit Confirmed — startedAt must not jump.
    tracker.noteState(e, compactphone::sip::CallState::Confirmed, 9000);
    const auto history =
        tracker.noteState(e, compactphone::sip::CallState::Disconnected, 10000);

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->startedAt, 500);
    EXPECT_EQ(history->durationMs, 9500);
}

TEST(CallSessionTracker, EraseBeforeDisconnectDropsTheSession)
{
    compactphone::sip::CallSessionTracker tracker;
    const auto e = entry(13, 70, "sip:gone@example.com",
                         compactphone::sip::CallDirection::Outbound);

    tracker.noteOutbound(e.id, e.accountId, e.remoteUri, 100);
    tracker.erase(e.id);
    // After erase, a bare Disconnected (no entry details) has nothing to log.
    compactphone::sip::CallEntry bare;
    bare.id = e.id;
    EXPECT_FALSE(tracker.noteState(
        bare, compactphone::sip::CallState::Disconnected, 500));
}

TEST(CallSessionTracker, PreservesRemoteDisplayName)
{
    compactphone::sip::CallSessionTracker tracker;
    compactphone::sip::CallEntry e;
    e.id = 14;
    e.accountId = 80;
    e.remoteUri = "sip:bob@example.com";
    e.remoteDisplayName = "Bob Roberts";
    e.direction = compactphone::sip::CallDirection::Inbound;

    tracker.noteIncoming(e, 100);
    tracker.noteState(e, compactphone::sip::CallState::Confirmed, 200);
    const auto history =
        tracker.noteState(e, compactphone::sip::CallState::Disconnected, 500);

    ASSERT_TRUE(history.has_value());
    EXPECT_EQ(history->remoteDisplayName, "Bob Roberts");
}

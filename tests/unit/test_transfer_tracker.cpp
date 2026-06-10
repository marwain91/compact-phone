#include <gtest/gtest.h>

#include "core/TransferTracker.h"

#include <cstdint>
#include <vector>

// These tests pin the transfer-status decision CallManager::handleTransferStatus
// delegates to: given the bookkeeping recorded at REFER time and a
// transfer-progress NOTIFY (status code + finalNotify), exactly which call legs
// get hung up. CallManager itself hangs up precisely the returned ids, so a
// regression here either tears down a live call on a failed transfer or leaks
// two live calls after a successful one — previously only observable in the
// live integration suite.

using compactphone::sip::TransferTracker;

namespace {
const std::vector<std::int32_t> kBlindLegs{7};
const std::vector<std::int32_t> kAttendedLegs{7, 9};
} // namespace

TEST(TransferTracker, FinalSuccessHangsUpBlindTransferLeg)
{
    TransferTracker t;
    t.record(7, kBlindLegs); // blindTransfer records the originating call
    EXPECT_EQ(t.takeLegsToHangup(7, 200, true), kBlindLegs);
    EXPECT_FALSE(t.has(7)); // entry consumed
}

TEST(TransferTracker, FinalSuccessHangsUpBothAttendedTransferLegs)
{
    TransferTracker t;
    t.record(7, kAttendedLegs); // attendedTransfer records active + consult
    EXPECT_EQ(t.takeLegsToHangup(7, 200, true), kAttendedLegs);
    EXPECT_FALSE(t.has(7));
}

// A non-final NOTIFY (e.g. "100 Trying" / "180 Ringing" sipfrag, or even a
// 2xx with more notifications pending) decides nothing: no leg may be hung
// up and the bookkeeping must stay pending for the final NOTIFY.
TEST(TransferTracker, NonFinalNotifyHangsUpNothingAndStaysPending)
{
    for (const int status : {100, 180, 200, 302, 486, 603}) {
        TransferTracker t;
        t.record(7, kAttendedLegs);
        EXPECT_TRUE(t.takeLegsToHangup(7, status, false).empty())
            << "status " << status;
        EXPECT_TRUE(t.has(7)) << "status " << status;
        // The final 2xx that follows must still find the legs.
        EXPECT_EQ(t.takeLegsToHangup(7, 200, true), kAttendedLegs)
            << "status " << status;
    }
}

// A final non-2xx NOTIFY means the transfer failed (busy, declined,
// redirected): the user keeps talking, so no leg may be hung up — but the
// REFER subscription is over, so the entry must be consumed, never to
// resurrect on a stray later NOTIFY.
TEST(TransferTracker, FinalFailureHangsUpNothingButConsumesEntry)
{
    for (const int status : {302, 486, 603}) {
        TransferTracker t;
        t.record(7, kAttendedLegs);
        EXPECT_TRUE(t.takeLegsToHangup(7, status, true).empty())
            << "status " << status;
        EXPECT_FALSE(t.has(7)) << "status " << status;
        // A duplicate/stray final success afterwards must not hang up legs
        // whose transfer already failed.
        EXPECT_TRUE(t.takeLegsToHangup(7, 200, true).empty())
            << "status " << status;
    }
}

TEST(TransferTracker, UnknownTransferIdHangsUpNothing)
{
    TransferTracker t;
    EXPECT_TRUE(t.takeLegsToHangup(7, 200, true).empty());
}

// A second final NOTIFY after a successful take must be a no-op — the legs
// were already handed out for hangup once.
TEST(TransferTracker, DuplicateFinalNotifyIsIdempotent)
{
    TransferTracker t;
    t.record(7, kBlindLegs);
    EXPECT_EQ(t.takeLegsToHangup(7, 200, true), kBlindLegs);
    EXPECT_TRUE(t.takeLegsToHangup(7, 200, true).empty());
}

// Boundary statuses of the 2xx gate: 199 and 300 are failures, 299 is
// still a success.
TEST(TransferTracker, TwoXxGateBoundaries)
{
    TransferTracker t;
    t.record(7, kBlindLegs);
    EXPECT_TRUE(t.takeLegsToHangup(7, 199, true).empty());

    t.record(7, kBlindLegs);
    EXPECT_TRUE(t.takeLegsToHangup(7, 300, true).empty());

    t.record(7, kBlindLegs);
    EXPECT_EQ(t.takeLegsToHangup(7, 299, true), kBlindLegs);
}

// xfer() failure rolls the bookkeeping back via drop(); a final NOTIFY
// arriving afterwards must find nothing.
TEST(TransferTracker, DroppedEntryHangsUpNothing)
{
    TransferTracker t;
    t.record(7, kBlindLegs);
    t.drop(7);
    EXPECT_TRUE(t.takeLegsToHangup(7, 200, true).empty());
}

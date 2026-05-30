#include <gtest/gtest.h>

#include "core/CallEntry.h"
#include "core/CallSnapshotSource.h"
#include "models/CallsModel.h"

#include <QCoreApplication>
#include <QSignalSpy>

#include <vector>

namespace {

// QSignalSpy needs a QCoreApplication instance for the metatype/signal
// machinery. A QCoreApplication (not QGuiApplication) is enough — these are
// pure model + signal tests with no GUI, so no qpa platform plugin is loaded
// (which is why this runs in the headless dev container). One instance for the
// whole test binary.
QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_calls_model";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = new QCoreApplication(argc, argv);
    return app;
}

// Fake snapshot source: returns whatever the test stages, with no SIP stack.
// This is the whole point of the CallSnapshotSource seam — CallsModel::refresh
// (the incremental insert/remove/dataChanged diff) becomes unit-testable.
class FakeCallSnapshotSource : public compactphone::sip::CallSnapshotSource {
public:
    std::vector<compactphone::sip::CallEntry> next;
    std::vector<compactphone::sip::CallEntry> snapshot() const override { return next; }
};

compactphone::sip::CallEntry makeEntry(
    compactphone::sip::CallId id,
    compactphone::sip::CallState state = compactphone::sip::CallState::Calling)
{
    compactphone::sip::CallEntry e;
    e.id = id;
    e.accountId = 1;
    e.remoteUri = "sip:600@pbx";
    e.remoteDisplayName = "Echo";
    e.state = state;
    return e;
}

using compactphone::models::CallsModel;

} // namespace

TEST(CallsModelTest, RoleNamesExposeExpectedKeys)
{
    ensureApp();
    FakeCallSnapshotSource src;
    CallsModel model(&src);
    const auto roles = model.roleNames();
    EXPECT_EQ(roles.value(CallsModel::IdRole), "callId");
    EXPECT_EQ(roles.value(CallsModel::StateRole), "state");
    EXPECT_EQ(roles.value(CallsModel::MutedRole), "muted");
    EXPECT_EQ(roles.value(CallsModel::DirectionRole), "direction");
}

// Constructing with an empty source yields zero rows (refresh runs in the ctor).
TEST(CallsModelTest, StartsEmptyWhenSourceHasNoCalls)
{
    ensureApp();
    FakeCallSnapshotSource src;
    CallsModel model(&src);
    EXPECT_EQ(model.rowCount(), 0);
}

// A brand-new call in the source must appear as an inserted row, and the role
// data must map through correctly (state "calling" -> exposed as "calling").
TEST(CallsModelTest, RefreshInsertsNewCallAndMapsRoles)
{
    ensureApp();
    FakeCallSnapshotSource src;
    CallsModel model(&src);

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    src.next = {makeEntry(7, compactphone::sip::CallState::Confirmed)};
    model.refresh();

    ASSERT_EQ(model.rowCount(), 1);
    EXPECT_EQ(inserted.count(), 1);
    const auto idx = model.index(0);
    EXPECT_EQ(model.data(idx, CallsModel::IdRole).toInt(), 7);
    EXPECT_EQ(model.data(idx, CallsModel::StateRole).toString(), "active");
    EXPECT_EQ(model.data(idx, CallsModel::RemoteDisplayNameRole).toString(), "Echo");
    EXPECT_FALSE(model.data(idx, CallsModel::HeldRole).toBool());
}

// A call that disappears from the source must be removed as a row.
TEST(CallsModelTest, RefreshRemovesEndedCall)
{
    ensureApp();
    FakeCallSnapshotSource src;
    src.next = {makeEntry(1), makeEntry(2)};
    CallsModel model(&src);
    ASSERT_EQ(model.rowCount(), 2);

    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    src.next = {makeEntry(2)};
    model.refresh();

    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(removed.count(), 1);
    EXPECT_EQ(model.data(model.index(0), CallsModel::IdRole).toInt(), 2);
}

// An existing call whose fields change must NOT be re-inserted (delegate state
// is preserved). Instead refresh emits dataChanged for exactly the changed
// roles. This is the incremental-diff contract the comment in refresh() spells
// out; pin it so a rewrite that fell back to a full reset is caught.
TEST(CallsModelTest, RefreshUpdatesInPlaceWithDataChangedNotReinsert)
{
    ensureApp();
    FakeCallSnapshotSource src;
    src.next = {makeEntry(5, compactphone::sip::CallState::Confirmed)};
    CallsModel model(&src);
    ASSERT_EQ(model.rowCount(), 1);

    QSignalSpy inserted(&model, &QAbstractItemModel::rowsInserted);
    QSignalSpy removed(&model, &QAbstractItemModel::rowsRemoved);
    QSignalSpy changed(&model, &QAbstractItemModel::dataChanged);

    // Same call id, but now held + muted.
    auto held = makeEntry(5, compactphone::sip::CallState::Confirmed);
    held.held = true;
    held.muted = true;
    src.next = {held};
    model.refresh();

    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_EQ(inserted.count(), 0);   // not re-inserted
    EXPECT_EQ(removed.count(), 0);    // not removed
    ASSERT_EQ(changed.count(), 1);    // one dataChanged for the row

    // The changed-roles list must contain Held and Muted (and only changed ones).
    const auto args = changed.takeFirst();
    const auto roles = args.at(2).value<QList<int>>();
    EXPECT_TRUE(roles.contains(static_cast<int>(CallsModel::HeldRole)));
    EXPECT_TRUE(roles.contains(static_cast<int>(CallsModel::MutedRole)));
    EXPECT_FALSE(roles.contains(static_cast<int>(CallsModel::StateRole)));

    EXPECT_TRUE(model.data(model.index(0), CallsModel::HeldRole).toBool());
    EXPECT_TRUE(model.data(model.index(0), CallsModel::MutedRole).toBool());
}

// A null source is tolerated (refresh treats it as "no calls"), so the model
// degrades safely rather than dereferencing a null pointer.
TEST(CallsModelTest, NullSourceYieldsEmptyModel)
{
    ensureApp();
    CallsModel model(nullptr);
    EXPECT_EQ(model.rowCount(), 0);
    model.refresh();
    EXPECT_EQ(model.rowCount(), 0);
}

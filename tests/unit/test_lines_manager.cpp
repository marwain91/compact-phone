#include <gtest/gtest.h>

#include "core/AccountsManager.h"
#include "core/LinesManager.h"
#include "core/WatchedLine.h"
#include "core/platform/Keychain_memory.h"
#include "core/sipbackend/fake/FakeSipBackend.h"
#include "persistence/Database.h"

#include <QCoreApplication>

namespace {
QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_lines_manager";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}
} // namespace

TEST(LinesManagerTest, AddListFindRemoveAndReload)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());

    compactphone::sip::LinesManager manager(&db, nullptr);
    int changedSignals = 0;
    QObject::connect(&manager, &compactphone::sip::LinesManager::linesChanged,
                     [&] { ++changedSignals; });

    const auto first = manager.add(7, "sip:1001@example.com", "Support");
    const auto second = manager.add(7, "sip:1002@example.com", "Sales");
    ASSERT_NE(first, compactphone::sip::kInvalidWatchedLineId);
    ASSERT_NE(second, compactphone::sip::kInvalidWatchedLineId);
    EXPECT_EQ(changedSignals, 2);

    const auto lines = manager.list();
    ASSERT_EQ(lines.size(), 2u);
    EXPECT_EQ(lines[0].id, first);
    EXPECT_EQ(lines[0].accountId, 7);
    EXPECT_EQ(lines[0].uri, "sip:1001@example.com");
    EXPECT_EQ(lines[0].label, "Support");
    EXPECT_EQ(lines[0].sortOrder, 0);
    EXPECT_EQ(lines[0].state, compactphone::sip::LineState::Unknown);
    EXPECT_EQ(lines[1].id, second);
    EXPECT_EQ(lines[1].sortOrder, 1);

    const auto found = manager.find(second);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->label, "Sales");

    EXPECT_TRUE(manager.remove(first));
    EXPECT_FALSE(manager.remove(first));
    EXPECT_EQ(changedSignals, 3);

    compactphone::sip::LinesManager reloaded(&db, nullptr);
    const auto reloadedLines = reloaded.list();
    ASSERT_EQ(reloadedLines.size(), 1u);
    EXPECT_EQ(reloadedLines[0].id, second);
    EXPECT_EQ(reloadedLines[0].uri, "sip:1002@example.com");
    EXPECT_EQ(reloadedLines[0].label, "Sales");
}

TEST(LinesManagerTest, HandlesMissingDatabaseGracefully)
{
    compactphone::sip::LinesManager manager(nullptr, nullptr);

    EXPECT_EQ(manager.add(1, "sip:1001@example.com", "Support"),
              compactphone::sip::kInvalidWatchedLineId);
    EXPECT_FALSE(manager.remove(1));
    EXPECT_TRUE(manager.list().empty());
    EXPECT_FALSE(manager.find(1).has_value());
}

// Drives a presence NOTIFY through the whole phase-5 path: FakeSipBackend
// onPresence -> AccountsManager::presenceChanged -> LinesManager updates the
// watched line's state. The pj::Buddy SUBSCRIBE itself lives in the PJSIP
// adapter and is exercised by the integration contract suite.
TEST(LinesManagerTest, PresenceUpdatesLineStateThroughBackend)
{
    ensureApp();
    compactphone::sipbackend::FakeSipBackend backend;
    ASSERT_TRUE(backend.start({}));
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::platform::MemoryKeychain kc;

    compactphone::sip::AccountsManager am(&backend, &db, &kc);
    backend.setListener(&am);

    // Register an account so the line's watch resolves to a backend account.
    compactphone::sip::Account a;
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = true;
    a.registerOnStartup = true;
    const auto accId = am.add(a, "secret");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);
    backend.simulateRegState(backend.lastAddedAccountId(), /*regActive=*/true,
                             /*sipCode=*/200, "OK");
    QCoreApplication::processEvents();

    compactphone::sip::LinesManager lines(&db, &am, &backend);
    int changed = 0;
    QObject::connect(&lines, &compactphone::sip::LinesManager::linesChanged,
                     [&] { ++changed; });

    const auto lineId = lines.add(accId, "sip:2000@pbx.example.com", "Ops");
    ASSERT_NE(lineId, compactphone::sip::kInvalidWatchedLineId);
    ASSERT_TRUE(lines.find(lineId).has_value());
    EXPECT_EQ(lines.find(lineId)->state, compactphone::sip::LineState::Unknown);

    // The fake mints WatchIds from 1; this line's watch is the first and only
    // one, so its backend WatchId is 1.
    backend.simulatePresence(1, compactphone::sipbackend::PresenceState::Busy);
    QCoreApplication::processEvents();

    EXPECT_EQ(lines.find(lineId)->state, compactphone::sip::LineState::Busy);
    EXPECT_GE(changed, 2);  // add + presence change

    backend.setListener(nullptr);
}

TEST(LinesManagerTest, LineStateStringsMatchQmlContract)
{
    EXPECT_STREQ(compactphone::sip::lineStateToString(
                     compactphone::sip::LineState::Unknown),
                 "unknown");
    EXPECT_STREQ(compactphone::sip::lineStateToString(
                     compactphone::sip::LineState::Idle),
                 "idle");
    EXPECT_STREQ(compactphone::sip::lineStateToString(
                     compactphone::sip::LineState::Busy),
                 "busy");
    EXPECT_STREQ(compactphone::sip::lineStateToString(
                     compactphone::sip::LineState::Offline),
                 "offline");
}

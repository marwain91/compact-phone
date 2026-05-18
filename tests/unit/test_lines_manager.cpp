#include <gtest/gtest.h>

#include "core/LinesManager.h"
#include "core/WatchedLine.h"
#include "persistence/Database.h"

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

#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/CallEntry.h"
#include "core/HistoryEntry.h"
#include "core/HistoryManager.h"
#include "persistence/Database.h"

#include <sqlite3.h>

class HistoryManagerTest : public ::testing::Test {
protected:
    compactphone::persistence::Database db;
    void SetUp() override
    {
        ASSERT_TRUE(db.openInMemory());
        // Seed an account row so the foreign key in call_history is satisfiable.
        sqlite3_stmt *stmt = nullptr;
        sqlite3_prepare_v2(db.handle(),
            "INSERT INTO accounts (display_name, username, domain, "
            "password_ref, transport, srtp_mode, dtmf_method) "
            "VALUES ('seed','seed','d','ref','udp','optional','rfc2833')",
            -1, &stmt, nullptr);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
};

TEST_F(HistoryManagerTest, AppendAndList)
{
    compactphone::sip::HistoryManager mgr(&db);
    compactphone::sip::HistoryEntry e;
    e.accountId = 1;
    e.direction = compactphone::sip::CallDirection::Outbound;
    e.remoteUri = "sip:600@asterisk:5060";
    e.startedAt = 1700000000000LL;
    e.durationMs = 5000;
    const auto id = mgr.append(e);
    ASSERT_NE(id, compactphone::sip::kInvalidHistoryId);

    const auto entries = mgr.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries[0].remoteUri, "sip:600@asterisk:5060");
    EXPECT_EQ(entries[0].durationMs, 5000);
    EXPECT_EQ(entries[0].direction, compactphone::sip::CallDirection::Outbound);
}

TEST_F(HistoryManagerTest, ListIsNewestFirst)
{
    compactphone::sip::HistoryManager mgr(&db);
    for (int i = 0; i < 3; ++i) {
        compactphone::sip::HistoryEntry e;
        e.accountId = 1;
        e.direction = compactphone::sip::CallDirection::Outbound;
        e.remoteUri = "sip:" + std::to_string(i) + "@asterisk:5060";
        e.startedAt = 1700000000000LL + i * 1000;
        mgr.append(e);
    }
    const auto entries = mgr.list();
    ASSERT_EQ(entries.size(), 3u);
    EXPECT_EQ(entries[0].remoteUri, "sip:2@asterisk:5060");
    EXPECT_EQ(entries[2].remoteUri, "sip:0@asterisk:5060");
}

TEST_F(HistoryManagerTest, FindByIdReturnsFullEntry)
{
    compactphone::sip::HistoryManager mgr(&db);
    compactphone::sip::HistoryEntry e;
    e.accountId = 1;
    e.direction = compactphone::sip::CallDirection::Inbound;
    e.remoteUri = "sip:caller@example.com";
    e.remoteDisplayName = "Caller";
    e.startedAt = 1700000010000LL;
    e.durationMs = 2500;
    e.endReason = "busy";
    const auto id = mgr.append(e);
    ASSERT_NE(id, compactphone::sip::kInvalidHistoryId);

    auto found = mgr.findById(id);
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->id, id);
    EXPECT_EQ(found->accountId, 1);
    EXPECT_EQ(found->direction, compactphone::sip::CallDirection::Inbound);
    EXPECT_EQ(found->remoteUri, "sip:caller@example.com");
    EXPECT_EQ(found->remoteDisplayName, "Caller");
    EXPECT_EQ(found->startedAt, 1700000010000LL);
    EXPECT_EQ(found->durationMs, 2500);
    EXPECT_EQ(found->endReason, "busy");
    EXPECT_FALSE(mgr.findById(9999).has_value());
}

TEST_F(HistoryManagerTest, ListHonorsLimit)
{
    compactphone::sip::HistoryManager mgr(&db);
    for (int i = 0; i < 5; ++i) {
        compactphone::sip::HistoryEntry e;
        e.accountId = 1;
        e.direction = compactphone::sip::CallDirection::Outbound;
        e.remoteUri = "sip:limit-" + std::to_string(i) + "@example.com";
        e.startedAt = 1700000000000LL + i;
        ASSERT_NE(mgr.append(e), compactphone::sip::kInvalidHistoryId);
    }

    const auto entries = mgr.list(2);
    ASSERT_EQ(entries.size(), 2u);
    EXPECT_EQ(entries[0].remoteUri, "sip:limit-4@example.com");
    EXPECT_EQ(entries[1].remoteUri, "sip:limit-3@example.com");
}

TEST_F(HistoryManagerTest, PrunesToMaxEntries)
{
    using compactphone::sip::HistoryManager;
    HistoryManager mgr(&db);
    const int overflow = HistoryManager::kMaxEntries + 12;
    for (int i = 0; i < overflow; ++i) {
        compactphone::sip::HistoryEntry e;
        e.accountId = 1;
        e.direction = compactphone::sip::CallDirection::Outbound;
        e.remoteUri = "sip:keep-" + std::to_string(i) + "@example.com";
        e.startedAt = 1700000000000LL + i * 1000;
        ASSERT_NE(mgr.append(e), compactphone::sip::kInvalidHistoryId);
    }

    // Ask for far more than the cap; the table itself must be bounded.
    const auto entries = mgr.list(1000);
    ASSERT_EQ(entries.size(), static_cast<size_t>(HistoryManager::kMaxEntries));
    // Newest survives, oldest is pruned.
    EXPECT_EQ(entries.front().remoteUri,
              "sip:keep-" + std::to_string(overflow - 1) + "@example.com");
    EXPECT_EQ(entries.back().remoteUri,
              "sip:keep-" + std::to_string(overflow - HistoryManager::kMaxEntries)
                  + "@example.com");
}

TEST_F(HistoryManagerTest, PrunesEntriesOlderThanMaxAge)
{
    using compactphone::sip::HistoryManager;
    HistoryManager mgr(&db);
    const std::int64_t base = 2000000000000LL; // well past the age window

    compactphone::sip::HistoryEntry oldEntry;
    oldEntry.accountId = 1;
    oldEntry.direction = compactphone::sip::CallDirection::Outbound;
    oldEntry.remoteUri = "sip:stale@example.com";
    oldEntry.startedAt = base;
    ASSERT_NE(mgr.append(oldEntry), compactphone::sip::kInvalidHistoryId);

    // A fresh call far enough ahead pushes the old one outside the window.
    compactphone::sip::HistoryEntry freshEntry;
    freshEntry.accountId = 1;
    freshEntry.direction = compactphone::sip::CallDirection::Outbound;
    freshEntry.remoteUri = "sip:fresh@example.com";
    freshEntry.startedAt = base + HistoryManager::kMaxAgeMs + 1000;
    ASSERT_NE(mgr.append(freshEntry), compactphone::sip::kInvalidHistoryId);

    const auto entries = mgr.list();
    ASSERT_EQ(entries.size(), 1u);
    EXPECT_EQ(entries.front().remoteUri, "sip:fresh@example.com");
}

TEST(HistoryManagerStandalone, HandlesMissingDatabaseGracefully)
{
    compactphone::sip::HistoryManager mgr(nullptr);
    compactphone::sip::HistoryEntry e;
    e.accountId = 1;
    e.remoteUri = "sip:x@example.com";

    EXPECT_EQ(mgr.append(e), compactphone::sip::kInvalidHistoryId);
    EXPECT_TRUE(mgr.list().empty());
    EXPECT_FALSE(mgr.findById(1).has_value());
}

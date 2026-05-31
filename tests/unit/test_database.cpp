#include <gtest/gtest.h>

#include "persistence/Database.h"

#include <QTemporaryDir>

#include <sqlite3.h>

#include <set>
#include <string>

TEST(Database, OpensInMemoryAndRunsMigrations)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());

    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db.handle(),
                                 "SELECT COUNT(*) FROM accounts",
                                 -1, &stmt, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    EXPECT_EQ(sqlite3_column_int(stmt, 0), 0);
    sqlite3_finalize(stmt);
}

TEST(Database, ReportsCurrentSchemaVersion)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    EXPECT_EQ(db.currentVersion(), 1);
}

TEST(Database, MigrationsAreIdempotent)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    EXPECT_TRUE(db.runMigrations()); // already at latest, no-op
    EXPECT_EQ(db.currentVersion(), 1);
}

TEST(Database, CreatesServingIndexes)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());

    std::set<std::string> indexes;
    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(db.handle(),
                                 "SELECT name FROM sqlite_master WHERE type = 'index'",
                                 -1, &stmt, nullptr),
              SQLITE_OK);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (name) indexes.insert(name);
    }
    sqlite3_finalize(stmt);

    // Serving indexes for the hot lookups (findByUri, unreadCount).
    EXPECT_TRUE(indexes.count("idx_contacts_uri") == 1);
    EXPECT_TRUE(indexes.count("idx_messages_unread") == 1);
}

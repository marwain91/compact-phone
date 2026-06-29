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
    EXPECT_EQ(db.currentVersion(), 2);
}

TEST(Database, MigrationsAreIdempotent)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    EXPECT_TRUE(db.runMigrations()); // already at latest, no-op
    EXPECT_EQ(db.currentVersion(), 2);
}

// Regression: the `provider` column was added to the baseline schema in #67
// without a matching migration, so databases created by an earlier release
// stayed at version 1 with no `provider` column. Every query referencing it
// (loadFromDatabase / insertRow) then failed with "no such column: provider",
// which silently broke adding accounts. The migration must repair such a
// database in place without losing existing rows.
TEST(Database, UpgradesPreProviderDatabaseByAddingColumn)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    sqlite3 *h = db.handle();
    auto exec = [&](const char *sql) {
        return sqlite3_exec(h, sql, nullptr, nullptr, nullptr) == SQLITE_OK;
    };

    // Recreate the pre-#67 shape: an accounts table with no `provider`
    // column, holding one existing row, recorded at schema version 1.
    ASSERT_TRUE(exec("DROP TABLE accounts"));
    ASSERT_TRUE(exec(
        "CREATE TABLE accounts ("
        "  id INTEGER PRIMARY KEY,"
        "  label TEXT NOT NULL DEFAULT '',"
        "  display_name TEXT NOT NULL,"
        "  username TEXT NOT NULL,"
        "  domain TEXT NOT NULL,"
        "  password_ref TEXT NOT NULL,"
        "  is_default INTEGER NOT NULL DEFAULT 0,"
        "  enabled INTEGER NOT NULL DEFAULT 1)"));
    ASSERT_TRUE(exec("INSERT INTO accounts (display_name, username, domain, "
                     "password_ref) VALUES ('Old', '1001', 'pbx', 'ref-1')"));
    ASSERT_TRUE(exec("DELETE FROM schema_version"));
    ASSERT_TRUE(exec("INSERT INTO schema_version (version) VALUES (1)"));

    // Re-running migrations must add the missing column and advance the
    // version, leaving the pre-existing row intact.
    ASSERT_TRUE(db.runMigrations());
    EXPECT_EQ(db.currentVersion(), 2);

    sqlite3_stmt *stmt = nullptr;
    ASSERT_EQ(sqlite3_prepare_v2(
                  h, "SELECT provider FROM accounts WHERE username = '1001'",
                  -1, &stmt, nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_step(stmt), SQLITE_ROW);
    const auto *provider =
        reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
    EXPECT_STREQ(provider ? provider : "(null)", "");
    sqlite3_finalize(stmt);

    // A provider-qualified insert (the path that was failing) now succeeds.
    EXPECT_TRUE(exec("INSERT INTO accounts (display_name, username, domain, "
                     "password_ref, provider) "
                     "VALUES ('New', '1002', 'pbx', 'ref-2', 'daktela')"));
}

TEST(Database, MigrationToLatestIsIdempotentOnAlreadyUpgradedDatabase)
{
    // A database already carrying the `provider` column (a fresh install,
    // whose baseline includes it at version 1) must not error when the
    // upgrade migration runs — the column-existence guard makes it a no-op.
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    sqlite3 *h = db.handle();
    ASSERT_EQ(sqlite3_exec(h, "DELETE FROM schema_version", nullptr, nullptr,
                           nullptr),
              SQLITE_OK);
    ASSERT_EQ(sqlite3_exec(h, "INSERT INTO schema_version (version) VALUES (1)",
                           nullptr, nullptr, nullptr),
              SQLITE_OK);
    EXPECT_TRUE(db.runMigrations());
    EXPECT_EQ(db.currentVersion(), 2);
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

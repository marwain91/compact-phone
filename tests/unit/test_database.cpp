#include <gtest/gtest.h>

#include "persistence/Database.h"

#include <QTemporaryDir>

#include <sqlite3.h>

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
    EXPECT_EQ(db.currentVersion(), 7);
}

TEST(Database, MigrationsAreIdempotent)
{
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    EXPECT_TRUE(db.runMigrations()); // already at latest, no-op
    EXPECT_EQ(db.currentVersion(), 7);
}

TEST(Database, UpgradesLegacyVersionOneAccountSchema)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("legacy.db").toStdString();

    sqlite3 *raw = nullptr;
    ASSERT_EQ(sqlite3_open(path.c_str(), &raw), SQLITE_OK);
    const char *legacySql = R"SQL(
        CREATE TABLE schema_version (version INTEGER NOT NULL);
        INSERT INTO schema_version (version) VALUES (1);
        CREATE TABLE accounts (
            id INTEGER PRIMARY KEY,
            display_name TEXT NOT NULL,
            username TEXT NOT NULL,
            domain TEXT NOT NULL,
            auth_user TEXT,
            password_ref TEXT NOT NULL,
            transport TEXT NOT NULL DEFAULT 'udp',
            proxy TEXT,
            stun_server TEXT,
            register_on_startup INTEGER NOT NULL DEFAULT 1,
            srtp_mode TEXT NOT NULL DEFAULT 'optional',
            allow_untrusted_cert INTEGER NOT NULL DEFAULT 0,
            dtmf_method TEXT NOT NULL DEFAULT 'rfc2833',
            is_default INTEGER NOT NULL DEFAULT 0,
            enabled INTEGER NOT NULL DEFAULT 1,
            sort_order INTEGER NOT NULL DEFAULT 0
        );
    )SQL";
    char *err = nullptr;
    ASSERT_EQ(sqlite3_exec(raw, legacySql, nullptr, nullptr, &err), SQLITE_OK)
        << (err ? err : "");
    sqlite3_free(err);
    sqlite3_close(raw);

    compactphone::persistence::Database db;
    ASSERT_TRUE(db.open(path));
    EXPECT_EQ(db.currentVersion(), 7);

    sqlite3_stmt *stmt = nullptr;
    EXPECT_EQ(sqlite3_prepare_v2(db.handle(),
                                 "SELECT label, auth_realm, public_address, codecs, "
                                 "voicemail_number, register_interval_sec, "
                                 "keepalive_interval_sec, session_timers_enabled, "
                                 "publish_presence_enabled, ice_enabled, "
                                 "hide_caller_id, zrtp_enabled FROM accounts",
                                 -1, &stmt, nullptr),
              SQLITE_OK);
    sqlite3_finalize(stmt);
}

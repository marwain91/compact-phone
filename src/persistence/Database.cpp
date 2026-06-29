#include "Database.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <cstring>

namespace compactphone::persistence {

namespace {

// Baseline schema for a fresh install (version 1). Existing databases are
// brought forward by the entries in kMigrations below; the array length is the
// latest version, so there is no separate counter to drift out of sync.
//
// Caution learned the hard way: do NOT add a column to this baseline without
// also appending a migration that adds it to already-installed databases. The
// `provider` column was added here in #67 with no migration, so every database
// created by an earlier release stayed at version 1 without the column and hit
// "no such column: provider" on load and on add. Migration v2 repairs those.
constexpr const char *kBaselineSchema = R"SQL(
CREATE TABLE accounts (
    id INTEGER PRIMARY KEY,
    label TEXT NOT NULL DEFAULT '',
    provider TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL,
    username TEXT NOT NULL,
    domain TEXT NOT NULL,
    auth_user TEXT,
    auth_realm TEXT,
    password_ref TEXT NOT NULL,
    transport TEXT NOT NULL DEFAULT 'udp',
    proxy TEXT,
    stun_server TEXT,
    public_address TEXT,
    codecs TEXT,
    voicemail_number TEXT,
    register_on_startup INTEGER NOT NULL DEFAULT 1,
    register_interval_sec INTEGER NOT NULL DEFAULT 0,
    keepalive_interval_sec INTEGER NOT NULL DEFAULT 0,
    session_timers_enabled INTEGER NOT NULL DEFAULT 1,
    publish_presence_enabled INTEGER NOT NULL DEFAULT 0,
    ice_enabled INTEGER NOT NULL DEFAULT 0,
    hide_caller_id INTEGER NOT NULL DEFAULT 0,
    zrtp_enabled INTEGER NOT NULL DEFAULT 0,
    srtp_mode TEXT NOT NULL DEFAULT 'optional',
    allow_untrusted_cert INTEGER NOT NULL DEFAULT 0,
    dtmf_method TEXT NOT NULL DEFAULT 'rfc2833',
    is_default INTEGER NOT NULL DEFAULT 0,
    enabled INTEGER NOT NULL DEFAULT 1,
    sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE UNIQUE INDEX idx_accounts_default ON accounts(is_default) WHERE is_default = 1;

CREATE TABLE contacts (
    id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL,
    sip_uri TEXT NOT NULL,
    phone TEXT,
    notes TEXT,
    favorite INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_contacts_name ON contacts(display_name);
CREATE INDEX idx_contacts_uri ON contacts(sip_uri);

CREATE TABLE call_history (
    id INTEGER PRIMARY KEY,
    account_id INTEGER NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    direction TEXT NOT NULL,
    remote_uri TEXT NOT NULL,
    remote_display TEXT,
    started_at INTEGER NOT NULL,
    duration_ms INTEGER NOT NULL DEFAULT 0,
    end_reason TEXT
);
CREATE INDEX idx_history_started ON call_history(started_at DESC);

CREATE TABLE app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT INTO app_settings (key, value) VALUES ('log_level', 'info');
INSERT INTO app_settings (key, value) VALUES ('ringtone_enabled', '1');

CREATE TABLE messages (
    id INTEGER PRIMARY KEY,
    account_id INTEGER NOT NULL,
    peer_uri TEXT NOT NULL,
    direction TEXT NOT NULL CHECK (direction IN ('in', 'out')),
    body TEXT NOT NULL,
    created_at_ms INTEGER NOT NULL,
    read INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_messages_peer ON messages(peer_uri, created_at_ms DESC);
CREATE INDEX idx_messages_created ON messages(created_at_ms DESC);
CREATE INDEX idx_messages_unread ON messages(direction, read);

CREATE TABLE watched_lines (
    id INTEGER PRIMARY KEY,
    account_id INTEGER NOT NULL,
    uri TEXT NOT NULL,
    label TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_watched_lines_order ON watched_lines(sort_order, id);
)SQL";

bool execSql(sqlite3 *db, const char *sql)
{
    char *err = nullptr;
    const int rc = sqlite3_exec(db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("sqlite exec failed: {} ({})", err ? err : "?", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
}

// Whether `table` already has a column named `column`. Used to make a column-
// adding migration idempotent: a fresh install whose baseline already declares
// the column (recorded at version 1) must skip the ALTER rather than fail with
// "duplicate column name".
bool columnExists(sqlite3 *db, const char *table, const char *column)
{
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *name =
            reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (name && std::strcmp(name, column) == 0) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

// v2: add the `provider` column to databases that predate it (see the note on
// kBaselineSchema). Guarded by columnExists so it is a no-op on fresh installs
// whose baseline already created the column at version 1.
bool addProviderColumn(sqlite3 *db)
{
    if (columnExists(db, "accounts", "provider")) return true;
    return execSql(
        db, "ALTER TABLE accounts ADD COLUMN provider TEXT NOT NULL DEFAULT ''");
}

// One entry per schema version, applied in order: index i upgrades the
// database to version i + 1. The array length defines the latest version.
using MigrationFn = bool (*)(sqlite3 *db);
const MigrationFn kMigrations[] = {
    [](sqlite3 *db) { return execSql(db, kBaselineSchema); }, // v1
    [](sqlite3 *db) { return addProviderColumn(db); },        // v2
};
constexpr int kLatestVersion =
    static_cast<int>(sizeof(kMigrations) / sizeof(kMigrations[0]));

} // namespace

Database::Database() = default;

Database::~Database()
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
}

bool Database::open(const std::string &path)
{
    return openWithPath(path) && runMigrations();
}

bool Database::openInMemory()
{
    return openWithPath(":memory:") && runMigrations();
}

bool Database::openWithPath(const std::string &path)
{
    if (m_db) {
        sqlite3_close(m_db);
        m_db = nullptr;
    }
    int rc = sqlite3_open(path.c_str(), &m_db);
    if (rc != SQLITE_OK) {
        spdlog::error("sqlite3_open failed: {}", sqlite3_errmsg(m_db));
        sqlite3_close(m_db);
        m_db = nullptr;
        return false;
    }
    executeStatement("PRAGMA foreign_keys = ON");
    // WAL lets the UI thread read while a background thread appends history /
    // messages without blocking; NORMAL is the safe durability pairing for a
    // single-process desktop app. Both are no-ops for :memory: databases.
    executeStatement("PRAGMA journal_mode = WAL");
    executeStatement("PRAGMA synchronous = NORMAL");
    return true;
}

bool Database::executeStatement(const char *sql)
{
    return execSql(m_db, sql);
}

bool Database::runMigrations()
{
    if (!executeStatement(
            "CREATE TABLE IF NOT EXISTS schema_version (version INTEGER NOT NULL)")) {
        return false;
    }

    int current = readVersion();
    for (int v = current + 1; v <= kLatestVersion; ++v) {
        if (!executeStatement("BEGIN TRANSACTION")) return false;
        if (!kMigrations[v - 1](m_db)) {
            executeStatement("ROLLBACK");
            return false;
        }
        if (!writeVersion(v)) {
            executeStatement("ROLLBACK");
            return false;
        }
        if (!executeStatement("COMMIT")) return false;
        spdlog::info("Database migrated to version {}", v);
    }
    return true;
}

int Database::currentVersion() const
{
    return readVersion();
}

int Database::readVersion() const
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "SELECT MAX(version) FROM schema_version",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int version = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW &&
        sqlite3_column_type(stmt, 0) != SQLITE_NULL) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);
    return version;
}

bool Database::writeVersion(int v)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db, "INSERT INTO schema_version (version) VALUES (?)",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, v);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace compactphone::persistence

#include "Database.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::persistence {

namespace {

// Embedded migration scripts. Keep in order — index 0 = version 1.
constexpr const char *kMigration001 = R"SQL(
CREATE TABLE accounts (
    id INTEGER PRIMARY KEY,
    label TEXT NOT NULL DEFAULT '',
    display_name TEXT NOT NULL,
    username TEXT NOT NULL,
    domain TEXT NOT NULL,
    auth_user TEXT,
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
)SQL";

constexpr const char *kMigration002 = R"SQL(
CREATE TABLE contacts (
    id INTEGER PRIMARY KEY,
    display_name TEXT NOT NULL,
    sip_uri TEXT NOT NULL,
    phone TEXT,
    notes TEXT,
    favorite INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_contacts_name ON contacts(display_name);

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
)SQL";

constexpr const char *kMigration003 = R"SQL(
CREATE TABLE app_settings (
    key TEXT PRIMARY KEY,
    value TEXT NOT NULL
);
INSERT INTO app_settings (key, value) VALUES ('log_level', 'info');
INSERT INTO app_settings (key, value) VALUES ('ringtone_enabled', '1');
)SQL";

constexpr const char *kMigration004 = R"SQL(
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
)SQL";

constexpr const char *kMigration005 = R"SQL(
CREATE TABLE watched_lines (
    id INTEGER PRIMARY KEY,
    account_id INTEGER NOT NULL,
    uri TEXT NOT NULL,
    label TEXT NOT NULL DEFAULT '',
    sort_order INTEGER NOT NULL DEFAULT 0
);
CREATE INDEX idx_watched_lines_order ON watched_lines(sort_order, id);
)SQL";

const char *kMigrations[] = {kMigration001, kMigration002, kMigration003,
                             kMigration004, kMigration005};
constexpr int kSqlMigrationCount = sizeof(kMigrations) / sizeof(kMigrations[0]);
constexpr int kLatestVersion = 6;

bool columnExists(sqlite3 *db, const char *table, const char *column)
{
    const std::string sql = std::string("PRAGMA table_info(") + table + ")";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(db, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    bool found = false;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *name = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (name && column == std::string(name)) {
            found = true;
            break;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool addColumnIfMissing(sqlite3 *db,
                        const char *table,
                        const char *column,
                        const char *ddl)
{
    if (columnExists(db, table, column)) return true;
    char *err = nullptr;
    const int rc = sqlite3_exec(db, ddl, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("sqlite migration add column failed: {} ({})",
                      err ? err : "?", ddl);
        sqlite3_free(err);
        return false;
    }
    return true;
}

bool migrate004AccountColumns(sqlite3 *db)
{
    return addColumnIfMissing(db, "accounts", "label",
                              "ALTER TABLE accounts ADD COLUMN label TEXT NOT NULL DEFAULT ''")
        && addColumnIfMissing(db, "accounts", "public_address",
                              "ALTER TABLE accounts ADD COLUMN public_address TEXT")
        && addColumnIfMissing(db, "accounts", "codecs",
                              "ALTER TABLE accounts ADD COLUMN codecs TEXT")
        && addColumnIfMissing(db, "accounts", "voicemail_number",
                              "ALTER TABLE accounts ADD COLUMN voicemail_number TEXT")
        && addColumnIfMissing(db, "accounts", "register_interval_sec",
                              "ALTER TABLE accounts ADD COLUMN register_interval_sec INTEGER NOT NULL DEFAULT 0")
        && addColumnIfMissing(db, "accounts", "keepalive_interval_sec",
                              "ALTER TABLE accounts ADD COLUMN keepalive_interval_sec INTEGER NOT NULL DEFAULT 0")
        && addColumnIfMissing(db, "accounts", "session_timers_enabled",
                              "ALTER TABLE accounts ADD COLUMN session_timers_enabled INTEGER NOT NULL DEFAULT 1")
        && addColumnIfMissing(db, "accounts", "publish_presence_enabled",
                              "ALTER TABLE accounts ADD COLUMN publish_presence_enabled INTEGER NOT NULL DEFAULT 0")
        && addColumnIfMissing(db, "accounts", "ice_enabled",
                              "ALTER TABLE accounts ADD COLUMN ice_enabled INTEGER NOT NULL DEFAULT 0")
        && addColumnIfMissing(db, "accounts", "hide_caller_id",
                              "ALTER TABLE accounts ADD COLUMN hide_caller_id INTEGER NOT NULL DEFAULT 0")
        && addColumnIfMissing(db, "accounts", "zrtp_enabled",
                              "ALTER TABLE accounts ADD COLUMN zrtp_enabled INTEGER NOT NULL DEFAULT 0");
}

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
    return true;
}

bool Database::executeStatement(const char *sql)
{
    char *err = nullptr;
    const int rc = sqlite3_exec(m_db, sql, nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        spdlog::error("sqlite exec failed: {} ({})", err ? err : "?", sql);
        sqlite3_free(err);
        return false;
    }
    return true;
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
        const bool migrationOk = v <= kSqlMigrationCount
            ? executeStatement(kMigrations[v - 1])
            : migrate004AccountColumns(m_db);
        if (!migrationOk) {
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

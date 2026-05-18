#pragma once

#include <string>

struct sqlite3;

namespace compactphone::persistence {

// Owns a single SQLite connection (via the raw sqlite3 C API) and runs
// schema migrations on open. Migrations are versioned monotonically; the
// `schema_version` table tracks the applied version. Statements are
// embedded as string constants in Database.cpp.
class Database {
public:
    Database();
    ~Database();

    Database(const Database &) = delete;
    Database &operator=(const Database &) = delete;

    // Opens (or creates) a SQLite database at `path` and runs pending
    // migrations. Returns true on success.
    bool open(const std::string &path);

    // Opens an in-memory database and runs migrations.
    bool openInMemory();

    // Re-runs migrations against the current handle. Idempotent.
    bool runMigrations();

    // Returns the highest applied migration version, or 0 if none.
    int currentVersion() const;

    // Returns the raw handle. Lifetime is tied to this Database object.
    sqlite3 *handle() const { return m_db; }

private:
    sqlite3 *m_db = nullptr;

    bool openWithPath(const std::string &path);
    int readVersion() const;
    bool writeVersion(int v);
    bool executeStatement(const char *sql);
};

} // namespace compactphone::persistence

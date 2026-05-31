#pragma once

#include <sqlite3.h>

#include <string>

namespace compactphone::persistence {

// Bind a std::string to a statement parameter. SQLITE_TRANSIENT copies the
// bytes, so the source string need not outlive the step.
inline void bindText(sqlite3_stmt *stmt, int idx, const std::string &s)
{
    sqlite3_bind_text(stmt, idx, s.data(), static_cast<int>(s.size()),
                      SQLITE_TRANSIENT);
}

// Read a text column as std::string, mapping a NULL column to "".
inline std::string readText(sqlite3_stmt *stmt, int col)
{
    const auto *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, col));
    return t ? std::string(t) : std::string{};
}

} // namespace compactphone::persistence

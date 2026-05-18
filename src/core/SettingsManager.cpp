#include "SettingsManager.h"
#include "persistence/Database.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

SettingsManager::SettingsManager(persistence::Database *db) : m_db(db) {}

std::optional<std::string> SettingsManager::get(const std::string &key) const
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT value FROM app_settings WHERE key = ?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                      SQLITE_TRANSIENT);
    std::optional<std::string> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *t = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        if (t) result = std::string(t);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::string SettingsManager::getOr(const std::string &key,
                                   const std::string &fallback) const
{
    return get(key).value_or(fallback);
}

bool SettingsManager::set(const std::string &key, const std::string &value)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "INSERT INTO app_settings (key, value) VALUES (?, ?) "
            "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_text(stmt, 1, key.data(), static_cast<int>(key.size()),
                      SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, value.data(), static_cast<int>(value.size()),
                      SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

} // namespace compactphone::sip

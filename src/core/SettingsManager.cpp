#include "SettingsManager.h"
#include "persistence/Database.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

SettingsManager::SettingsManager(persistence::Database *db) : m_db(db) {}

void SettingsManager::ensureLoaded() const
{
    if (m_loaded) return;
    m_loaded = true;
    if (!m_db || !m_db->handle()) return;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT key, value FROM app_settings", -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const auto *k = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0));
        const auto *v = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 1));
        if (k && v) m_cache[k] = v;
    }
    sqlite3_finalize(stmt);
}

std::optional<std::string> SettingsManager::get(const std::string &key) const
{
    ensureLoaded();
    auto it = m_cache.find(key);
    if (it == m_cache.end()) return std::nullopt;
    return it->second;
}

std::string SettingsManager::getOr(const std::string &key,
                                   const std::string &fallback) const
{
    return get(key).value_or(fallback);
}

bool SettingsManager::set(const std::string &key, const std::string &value)
{
    ensureLoaded();
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
    if (ok) m_cache[key] = value; // write-through
    return ok;
}

} // namespace compactphone::sip

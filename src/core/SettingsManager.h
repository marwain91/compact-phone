#pragma once

#include <optional>
#include <string>
#include <unordered_map>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

// Tiny key/value store over the app_settings table. The whole table is loaded
// into an in-memory cache on first access (one SELECT) and reads are served
// from memory; writes go write-through to SQLite and update the cache. This
// avoids a prepare/step/finalize round-trip per key — the controllers read
// ~15 settings at startup.
class SettingsManager {
public:
    explicit SettingsManager(persistence::Database *db);

    SettingsManager(const SettingsManager &) = delete;
    SettingsManager &operator=(const SettingsManager &) = delete;

    std::optional<std::string> get(const std::string &key) const;
    std::string getOr(const std::string &key, const std::string &fallback) const;
    bool set(const std::string &key, const std::string &value);

private:
    void ensureLoaded() const;

    persistence::Database *m_db;
    mutable std::unordered_map<std::string, std::string> m_cache;
    mutable bool m_loaded = false;
};

} // namespace compactphone::sip

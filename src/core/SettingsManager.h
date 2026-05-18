#pragma once

#include <optional>
#include <string>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

// Tiny key/value store over the app_settings table.
class SettingsManager {
public:
    explicit SettingsManager(persistence::Database *db);

    SettingsManager(const SettingsManager &) = delete;
    SettingsManager &operator=(const SettingsManager &) = delete;

    std::optional<std::string> get(const std::string &key) const;
    std::string getOr(const std::string &key, const std::string &fallback) const;
    bool set(const std::string &key, const std::string &value);

private:
    persistence::Database *m_db;
};

} // namespace compactphone::sip

#pragma once

#include "Keychain.h"

#include <unordered_map>

namespace compactphone::platform {

// In-memory keychain for tests and ephemeral processes. Loses data on
// destruction; not suitable for production.
class MemoryKeychain : public IKeychain {
public:
    std::optional<std::string> get(const std::string &ref) override;
    bool set(const std::string &ref, const std::string &password) override;
    bool erase(const std::string &ref) override;

private:
    std::unordered_map<std::string, std::string> m_store;
};

} // namespace compactphone::platform

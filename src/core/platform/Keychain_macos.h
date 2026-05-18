#pragma once

#include "Keychain.h"

namespace compactphone::platform {

// Apple Keychain Services backend (macOS / iOS). Stores SIP credentials as
// kSecClassGenericPassword items with service = "Compact Phone" and
// account = caller-supplied reference. Survives uninstalls until the user
// removes the keychain entry manually.
class MacKeychain final : public IKeychain {
public:
    MacKeychain();
    ~MacKeychain() override;

    std::optional<std::string> get(const std::string &ref) override;
    bool set(const std::string &ref, const std::string &password) override;
    bool erase(const std::string &ref) override;
};

} // namespace compactphone::platform

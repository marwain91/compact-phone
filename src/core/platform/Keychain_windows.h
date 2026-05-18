#pragma once

#include "Keychain.h"

namespace compactphone::platform {

// Windows Credential Manager backend. Items live under the user's
// generic-credentials store with TargetName =
// "eu.havliczech.compactphone:<ref>". Persists across reboots; tied to
// the Windows user profile.
class WindowsKeychain final : public IKeychain {
public:
    WindowsKeychain();
    ~WindowsKeychain() override;

    std::optional<std::string> get(const std::string &ref) override;
    bool set(const std::string &ref, const std::string &password) override;
    bool erase(const std::string &ref) override;
};

} // namespace compactphone::platform

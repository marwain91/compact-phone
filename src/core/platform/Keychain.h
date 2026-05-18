#pragma once

#include <optional>
#include <string>

namespace compactphone::platform {

// Stores SIP-account passwords keyed by an opaque reference string. The
// reference is stored in SQLite; the password value lives only here.
//
// v0.2: file-encrypted backend by default, in-memory for tests.
// v0.4: native backends — Security.framework (macOS), wincred (Windows),
// libsecret (Linux desktop). The file backend remains the dev-container
// fallback and the Linux fallback when libsecret is unavailable.
class IKeychain {
public:
    virtual ~IKeychain() = default;

    virtual std::optional<std::string> get(const std::string &ref) = 0;
    virtual bool set(const std::string &ref, const std::string &password) = 0;
    virtual bool erase(const std::string &ref) = 0;
};

} // namespace compactphone::platform

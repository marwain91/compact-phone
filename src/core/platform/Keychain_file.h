#pragma once

#include "Keychain.h"

#include <string>
#include <unordered_map>

namespace compactphone::platform {

// File-encrypted keychain. The file stores a JSON map { ref -> password }
// encrypted with AES-256-GCM. The key is derived via HKDF-SHA256 from a
// fixed master secret salted with a 16-byte random per-installation salt
// that lives in the first 16 bytes of the file. Suitable for the dev
// container and as a Linux fallback when libsecret is unavailable; not a
// substitute for an OS keychain on a real desktop deployment (a determined
// attacker with file read access can recover the key from the binary).
class FileKeychain : public IKeychain {
public:
    explicit FileKeychain(const std::string &path);

    // Reads and decrypts the file if it exists, or initializes an empty
    // store with a fresh salt. Returns false if the file is present but
    // the GCM auth tag fails (tampering or corruption).
    bool open();

    std::optional<std::string> get(const std::string &ref) override;
    bool set(const std::string &ref, const std::string &password) override;
    bool erase(const std::string &ref) override;

private:
    std::string m_path;
    std::unordered_map<std::string, std::string> m_store;
    std::string m_salt;   // 16 bytes

    bool persist();
};

} // namespace compactphone::platform

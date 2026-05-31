#pragma once

#include "Keychain.h"

#include <string>
#include <unordered_map>

namespace compactphone::platform {

// File-encrypted keychain. The file stores a JSON map { ref -> password }
// encrypted with AES-256-GCM. The key is derived via HKDF-SHA256 from a
// per-installation random 256-bit master key combined with a 16-byte random
// salt (the salt lives in the first 16 bytes of the keychain file; the master
// key lives in a sibling "<path>.key" file created with owner-only 0600
// permissions). Using a random per-install key — rather than a constant baked
// into the binary — means a key recovered from one install cannot decrypt any
// other install's secrets.
//
// Used on Linux (where there is no guaranteed OS keystore) and in the dev
// container. RESIDUAL LIMITATION: an attacker who can read BOTH the keychain
// file and its sibling .key file (i.e. has local read access to the user's
// app-data directory) can still decrypt the store. The encryption protects
// against the keychain file leaking alone (backup, sync, the public binary),
// not against full local file-read. On macOS/Windows the OS keystore is the
// authoritative backend and this file store is not used (see Keychain_factory).
class FileKeychain : public IKeychain {
public:
    explicit FileKeychain(const std::string &path);

    // Loads (or generates) the per-install master key, then reads and decrypts
    // the keychain file if it exists, or initializes an empty store with a
    // fresh salt. Returns false if the file is present but the GCM auth tag
    // fails (tampering or corruption), or if the master key can't be accessed.
    bool open();

    std::optional<std::string> get(const std::string &ref) override;
    bool set(const std::string &ref, const std::string &password) override;
    bool erase(const std::string &ref) override;

private:
    std::string m_path;
    std::unordered_map<std::string, std::string> m_store;
    std::string m_salt;        // 16 bytes
    std::string m_masterKey;   // 32 bytes, per-install, from <path>.key

    bool loadOrCreateMasterKey();
    bool persist();
};

} // namespace compactphone::platform

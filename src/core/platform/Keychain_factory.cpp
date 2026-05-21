#include "Keychain_factory.h"
#include "Keychain_file.h"

#if defined(__APPLE__)
#include "Keychain_macos.h"
#include "TouchIdGate.h"
#elif defined(_WIN32)
#include "Keychain_windows.h"
#endif

#include <spdlog/spdlog.h>

#include <memory>

namespace compactphone::platform {

#if defined(__APPLE__)

// Wraps the file-encrypted store as the primary keychain and only touches
// the macOS Security.framework keychain to migrate legacy items that were
// written by older builds. Once a ref lives in the file store, the macOS
// keychain is never queried for it again, which means:
//
//   - No more ACL prompts on every rebuilt binary signature.
//   - No more failed `kSecAttrAccessControl` writes on ad-hoc-signed dev
//     builds (which lack the keychain-sharing entitlement).
//
// The file store sits in AppDataLocation alongside the SQLite DB and is
// encrypted with AES-256-GCM via HKDF-SHA256. On a desktop where the user
// account is itself protected (FileVault or login password), that's an
// acceptable trade-off versus permanently breaking the user experience.
class HybridMacKeychain final : public IKeychain {
public:
    HybridMacKeychain(std::unique_ptr<FileKeychain> file,
                      std::unique_ptr<MacKeychain> mac)
        : m_file(std::move(file)), m_mac(std::move(mac)) {}

    std::optional<std::string> get(const std::string &ref) override
    {
        if (auto v = m_file->get(ref)) return v;
        if (!m_mac) return std::nullopt;
        auto v = m_mac->get(ref);
        if (!v) return std::nullopt;
        // First read after upgrade: lift the password into the file store
        // and remove the macOS keychain copy. Subsequent process launches
        // skip the macOS keychain entirely.
        if (!m_file->set(ref, *v)) {
            spdlog::warn("HybridMacKeychain: file write failed during "
                         "migration of {}; will retry next launch", ref);
            return v;
        }
        m_mac->erase(ref);
        spdlog::info("HybridMacKeychain: migrated {} from macOS keychain to "
                     "file store", ref);
        return v;
    }

    bool set(const std::string &ref, const std::string &password) override
    {
        // Writes go to the file store only. The macOS keychain copy (if
        // any) is removed so a later get() doesn't accidentally pick up a
        // stale credential.
        if (m_mac) m_mac->erase(ref);
        return m_file->set(ref, password);
    }

    bool erase(const std::string &ref) override
    {
        if (m_mac) m_mac->erase(ref);
        return m_file->erase(ref);
    }

private:
    std::unique_ptr<FileKeychain> m_file;
    std::unique_ptr<MacKeychain>  m_mac;
};

#endif // __APPLE__

std::unique_ptr<IKeychain> makeKeychain(const std::string &fallbackPath,
                                        bool forceFile)
{
    auto file = std::make_unique<FileKeychain>(fallbackPath);
    if (!file->open()) {
        spdlog::error("Keychain backend: FileKeychain open failed");
    }

    if (forceFile) {
        spdlog::info("Keychain backend: AES-256-GCM file ({}) [forced]",
                     fallbackPath);
        return file;
    }

#if defined(__APPLE__)
    spdlog::info("Keychain backend: Touch ID-gated file store (hybrid "
                 "migration from Security.framework on first read)");
    auto hybrid = std::make_unique<HybridMacKeychain>(
        std::move(file), std::make_unique<MacKeychain>());
    return wrapWithTouchId(std::move(hybrid));
#elif defined(_WIN32)
    spdlog::info("Keychain backend: Windows Credential Manager");
    return std::make_unique<WindowsKeychain>();
#else
    spdlog::info("Keychain backend: AES-256-GCM file ({})", fallbackPath);
    return file;
#endif
}

} // namespace compactphone::platform

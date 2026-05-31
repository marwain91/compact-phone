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

std::unique_ptr<IKeychain> makeKeychain(const std::string &fallbackPath,
                                        bool forceFile)
{
    // forceFile is used by headless/test contexts that must avoid touching an
    // OS keystore. Everywhere else the OS keystore is authoritative.
    if (forceFile) {
        auto file = std::make_unique<FileKeychain>(fallbackPath);
        if (!file->open()) {
            spdlog::error("Keychain backend: FileKeychain open failed");
        }
        spdlog::info("Keychain backend: AES-256-GCM file ({}) [forced]",
                     fallbackPath);
        return file;
    }

#if defined(__APPLE__)
    // macOS Security.framework is the authoritative store — credentials stay
    // in the OS Keychain (hardware-protected, ACL-gated) and are never copied
    // out into a weaker on-disk store.
    spdlog::info("Keychain backend: macOS Security.framework (Touch ID-gated)");
    return wrapWithTouchId(std::make_unique<MacKeychain>());
#elif defined(_WIN32)
    spdlog::info("Keychain backend: Windows Credential Manager");
    return std::make_unique<WindowsKeychain>();
#else
    // Linux has no guaranteed OS keystore: use the per-install-keyed file
    // store (see FileKeychain for the threat model and residual limitation).
    auto file = std::make_unique<FileKeychain>(fallbackPath);
    if (!file->open()) {
        spdlog::error("Keychain backend: FileKeychain open failed");
    }
    spdlog::info("Keychain backend: AES-256-GCM file ({})", fallbackPath);
    return file;
#endif
}

} // namespace compactphone::platform

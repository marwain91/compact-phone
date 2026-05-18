#include "Keychain_factory.h"
#include "Keychain_file.h"

#if defined(__APPLE__)
#include "Keychain_macos.h"
#elif defined(_WIN32)
#include "Keychain_windows.h"
#endif

#include <spdlog/spdlog.h>

namespace compactphone::platform {

std::unique_ptr<IKeychain> makeKeychain(const std::string &fallbackPath,
                                        bool forceFile)
{
#if defined(__APPLE__)
    if (!forceFile) {
        spdlog::info("Keychain backend: macOS Security.framework");
        return std::make_unique<MacKeychain>();
    }
#elif defined(_WIN32)
    if (!forceFile) {
        spdlog::info("Keychain backend: Windows Credential Manager");
        return std::make_unique<WindowsKeychain>();
    }
#endif

    spdlog::info("Keychain backend: AES-256-GCM file ({})", fallbackPath);
    auto kc = std::make_unique<FileKeychain>(fallbackPath);
    if (!kc->open()) {
        spdlog::error("Keychain backend: FileKeychain open failed");
    }
    return kc;
}

} // namespace compactphone::platform

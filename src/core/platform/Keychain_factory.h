#pragma once

#include "Keychain.h"

#include <memory>
#include <string>

namespace compactphone::platform {

// Picks the best keychain backend for the current platform.
//
//   macOS   → MacKeychain (Security.framework)
//   Windows → WindowsKeychain (wincred)
//   else    → FileKeychain  (AES-256-GCM encrypted file at `fallbackPath`)
//
// Set `forceFile = true` (e.g. from the CLI flag or Settings) to bypass the
// native backend; useful for headless Linux / debugging.
std::unique_ptr<IKeychain> makeKeychain(const std::string &fallbackPath,
                                        bool forceFile = false);

} // namespace compactphone::platform

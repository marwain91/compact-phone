#pragma once

#include "Autostart.h"

namespace compactphone::platform {

// Registers the running .app bundle as a login item via SMAppService
// (macOS 13+). Bundle-based, so it survives the app being moved.
class MacAutostart : public IAutostart {
public:
    bool isSupported() const override;
    bool isEnabled() const override;
    bool setEnabled(bool on) override;
};

} // namespace compactphone::platform

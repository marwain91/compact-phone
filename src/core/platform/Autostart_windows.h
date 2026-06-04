#pragma once

#include "Autostart.h"

namespace compactphone::platform {

// Writes/removes the HKCU\…\CurrentVersion\Run value via QSettings.
class WindowsAutostart : public IAutostart {
public:
    bool isSupported() const override { return true; }
    bool isEnabled() const override;
    bool setEnabled(bool on) override;
};

} // namespace compactphone::platform

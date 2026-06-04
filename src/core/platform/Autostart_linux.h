#pragma once

#include "Autostart.h"

#include <QString>

namespace compactphone::platform {

// Writes/removes ~/.config/autostart/compactphone.desktop (honours
// $XDG_CONFIG_HOME). Exec resolves to $APPIMAGE when set, else the running
// executable path.
class LinuxAutostart : public IAutostart {
public:
    bool isSupported() const override;
    bool isEnabled() const override;
    bool setEnabled(bool on) override;

private:
    static QString desktopFilePath();
    static QString execLine();
};

} // namespace compactphone::platform

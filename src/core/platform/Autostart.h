#pragma once

namespace compactphone::platform {

// Registers/removes a per-user OS "launch at login" entry for this app.
//
//   macOS   → SMAppService.mainApp (login item)
//   Windows → HKCU\…\CurrentVersion\Run value
//   Linux   → ~/.config/autostart/compactphone.desktop
//
// The OS entry is the source of truth: isEnabled() queries the OS, so a
// user toggling the login item via the OS UI is reflected back.
class IAutostart {
public:
    virtual ~IAutostart() = default;

    // False when the platform has no usable autostart mechanism (e.g. a
    // headless Linux session). The Settings UI hides the toggle when false.
    virtual bool isSupported() const = 0;

    // Current OS state.
    virtual bool isEnabled() const = 0;

    // Register (on=true) or remove (on=false) the login item.
    // Returns true on success.
    virtual bool setEnabled(bool on) = 0;
};

} // namespace compactphone::platform

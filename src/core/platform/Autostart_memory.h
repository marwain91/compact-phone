#pragma once

#include "Autostart.h"

namespace compactphone::platform {

// In-memory IAutostart for tests. Stores state in a member; can simulate a
// single setEnabled() failure and an unsupported platform.
class MemoryAutostart : public IAutostart {
public:
    bool isSupported() const override { return m_supported; }
    bool isEnabled() const override { return m_enabled; }
    bool setEnabled(bool on) override;

    // Test controls.
    void setSupported(bool supported) { m_supported = supported; }
    void failNextSetEnabled() { m_failNext = true; }

private:
    bool m_enabled = false;
    bool m_supported = true;
    bool m_failNext = false;
};

} // namespace compactphone::platform

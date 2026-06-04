#include "Autostart_memory.h"

namespace compactphone::platform {

bool MemoryAutostart::setEnabled(bool on)
{
    if (m_failNext) {
        m_failNext = false;
        return false;
    }
    m_enabled = on;
    return true;
}

} // namespace compactphone::platform

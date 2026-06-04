#include "Autostart_factory.h"

#if defined(__APPLE__)
#include "Autostart_macos.h"
#elif defined(_WIN32)
#include "Autostart_windows.h"
#else
#include "Autostart_linux.h"
#endif

namespace compactphone::platform {

std::unique_ptr<IAutostart> makeAutostart()
{
#if defined(__APPLE__)
    return std::make_unique<MacAutostart>();
#elif defined(_WIN32)
    return std::make_unique<WindowsAutostart>();
#else
    return std::make_unique<LinuxAutostart>();
#endif
}

} // namespace compactphone::platform

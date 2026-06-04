#include "Autostart_macos.h"

#import <ServiceManagement/ServiceManagement.h>

#include <spdlog/spdlog.h>

namespace compactphone::platform {

bool MacAutostart::isSupported() const
{
    if (@available(macOS 13.0, *)) return true;
    return false;
}

bool MacAutostart::isEnabled() const
{
    if (@available(macOS 13.0, *)) {
        return SMAppService.mainAppService.status == SMAppServiceStatusEnabled;
    }
    return false;
}

bool MacAutostart::setEnabled(bool on)
{
    if (@available(macOS 13.0, *)) {
        NSError *err = nil;
        const BOOL ok = on
            ? [SMAppService.mainAppService registerAndReturnError:&err]
            : [SMAppService.mainAppService unregisterAndReturnError:&err];
        if (!ok) {
            spdlog::warn("MacAutostart: {} failed: {}",
                         on ? "register" : "unregister",
                         err ? err.localizedDescription.UTF8String : "unknown");
            return false;
        }
        return true;
    }
    spdlog::warn("MacAutostart: SMAppService requires macOS 13+");
    return false;
}

} // namespace compactphone::platform

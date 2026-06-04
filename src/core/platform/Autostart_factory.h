#pragma once

#include "Autostart.h"

#include <memory>

namespace compactphone::platform {

// Returns the autostart backend for the current platform.
std::unique_ptr<IAutostart> makeAutostart();

} // namespace compactphone::platform

#include "PowerMonitor.h"

// Backends ship as siblings of this file:
//   PowerMonitor_macos.mm     → __APPLE__
//   PowerMonitor_windows.cpp  → _WIN32
//   (Linux DBus backend lives in PowerMonitor_linux.cpp when Qt6 DBus is
//    available; otherwise the stub here applies.)
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(COMPACTPHONE_WITH_LINUX_POWER)

namespace compactphone {

struct PowerMonitor::Impl {};

PowerMonitor::PowerMonitor(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>()) {}

PowerMonitor::~PowerMonitor() = default;

} // namespace compactphone

#endif

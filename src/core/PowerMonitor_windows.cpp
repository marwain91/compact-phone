// Windows PowerMonitor backend: listens for WM_POWERBROADCAST /
// PBT_APMRESUMESUSPEND via QAbstractNativeEventFilter. Active only when
// COMPACTPHONE_WITH_TRAY is on (QApplication is needed for native
// events); falls back to no-op on QGuiApplication-only builds.

#include "PowerMonitor.h"

#ifdef _WIN32

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>

#include <windows.h>

namespace compactphone {

namespace {

class PowerEventFilter final : public QAbstractNativeEventFilter {
public:
    explicit PowerEventFilter(PowerMonitor *owner) : m_owner(owner) {}

    bool nativeEventFilter(const QByteArray &eventType,
                           void *message,
                           qintptr *) override
    {
        if (eventType != "windows_generic_MSG"
            && eventType != "windows_dispatcher_MSG") {
            return false;
        }
        auto *msg = static_cast<MSG *>(message);
        if (msg->message == WM_POWERBROADCAST
            && msg->wParam == PBT_APMRESUMESUSPEND) {
            emit m_owner->wokeUp();
        }
        return false;
    }

private:
    PowerMonitor *m_owner;
};

} // namespace

struct PowerMonitor::Impl {
    std::unique_ptr<PowerEventFilter> filter;
};

PowerMonitor::PowerMonitor(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    m_impl->filter = std::make_unique<PowerEventFilter>(this);
    if (auto *app = QCoreApplication::instance()) {
        app->installNativeEventFilter(m_impl->filter.get());
    }
}

PowerMonitor::~PowerMonitor()
{
    if (m_impl && m_impl->filter) {
        if (auto *app = QCoreApplication::instance()) {
            app->removeNativeEventFilter(m_impl->filter.get());
        }
    }
}

} // namespace compactphone

#endif // _WIN32

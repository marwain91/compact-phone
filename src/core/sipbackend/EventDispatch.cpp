#include "EventDispatch.h"

#include <QObject>

namespace compactphone::sipbackend {

EventDispatch::EventDispatch() : m_dispatch(std::make_unique<QObject>()) {}

EventDispatch::~EventDispatch() = default;

void EventDispatch::post(std::function<void()> fn)
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);
    QMetaObject::invokeMethod(
        m_dispatch.get(),
        [this, epoch, fn = std::move(fn)] {
            if (epoch == m_epoch.load(std::memory_order_acquire))
                fn();
        },
        Qt::QueuedConnection);
}

} // namespace compactphone::sipbackend

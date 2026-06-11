#include "EventDispatch.h"

#include <QObject>

// Model the happens-before that Qt's queued-connection mutex provides but
// TSan cannot see because QtCore is uninstrumented.  The annotation is a
// no-op in non-TSan builds: the guard below ensures the header is never
// included and the calls are compiled away entirely.
#if defined(__SANITIZE_THREAD__) || \
    (defined(__has_feature) && __has_feature(thread_sanitizer))
#  define COMPACTPHONE_TSAN 1
#  include <sanitizer/tsan_interface.h>
#endif

namespace compactphone::sipbackend {

EventDispatch::EventDispatch() : m_dispatch(std::make_unique<QObject>()) {}

EventDispatch::~EventDispatch() = default;

void EventDispatch::post(std::function<void()> fn)
{
    const auto epoch = m_epoch.load(std::memory_order_acquire);

    // Announce that all writes visible on this (PJSIP) thread before this
    // point are "released" onto the dispatch token.  The corresponding
    // acquire inside the queued lambda below makes TSan treat the lambda's
    // reads as happening-after these writes — matching what Qt's event-queue
    // mutex actually guarantees at runtime.
#ifdef COMPACTPHONE_TSAN
    __tsan_release(m_dispatch.get());
#endif

    QMetaObject::invokeMethod(
        m_dispatch.get(),
        [this, epoch, fn = std::move(fn)] {
            // Paired acquire: everything the posting thread released above is
            // now visible here.  Covers every caller of post() without
            // needing per-caller suppressions, and masks nothing outside this
            // handoff path.
#ifdef COMPACTPHONE_TSAN
            __tsan_acquire(m_dispatch.get());
#endif
            if (epoch == m_epoch.load(std::memory_order_acquire))
                fn();
        },
        Qt::QueuedConnection);
}

} // namespace compactphone::sipbackend

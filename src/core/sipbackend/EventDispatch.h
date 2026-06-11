#pragma once

// Queued main-thread event delivery shared by every backend adapter.
//
// post(fn) queues fn onto the Qt main thread (the thread that constructed
// this object). Each queued lambda re-checks the epoch it was posted
// under; invalidate() bumps the epoch so everything queued before it is
// dropped. This implements boundary contract rules 2 and 4
// (see ISipBackend.h): events are never delivered synchronously, never
// re-entrantly, and never after stop()/setListener(nullptr).
//
// Destruction safety: the internal QObject is the invokeMethod context —
// destroying it cancels undelivered lambdas — so this member must be
// declared so it dies before anything the lambdas capture.
//
// Thread-safety: post() may be called from any thread (the PJSIP adapter
// posts from PJSUA worker threads); invalidate() and destruction are
// main-thread-only, matching the boundary contract.

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

class QObject;

namespace compactphone::sipbackend {

class EventDispatch {
public:
    EventDispatch();                 // must run on the Qt main thread
    ~EventDispatch();

    EventDispatch(const EventDispatch &) = delete;
    EventDispatch &operator=(const EventDispatch &) = delete;

    // Queue fn for main-thread delivery under the current epoch.
    void post(std::function<void()> fn);

    // Main-thread only: drop everything queued so far.
    void invalidate() { m_epoch.fetch_add(1, std::memory_order_release); }

private:
    std::atomic<std::uint64_t> m_epoch{0};
    std::unique_ptr<QObject> m_dispatch;
};

} // namespace compactphone::sipbackend

#include <gtest/gtest.h>

#include "core/sipbackend/EventDispatch.h"

#include <QCoreApplication>

#include <chrono>
#include <thread>

using compactphone::sipbackend::EventDispatch;

namespace {

QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_event_dispatch";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

// Pumps the event loop until pred holds or timeout elapses.
template <typename Pred>
bool pump(Pred pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return pred();
}

} // namespace

TEST(EventDispatchDelayed, DelayedPostFiresAfterDelay)
{
    ensureApp();
    EventDispatch d;
    bool fired = false;
    d.postDelayed(20, [&] { fired = true; });
    // Not synchronous and not immediate.
    EXPECT_FALSE(fired);
    EXPECT_TRUE(pump([&] { return fired; }, std::chrono::milliseconds(2000)));
}

TEST(EventDispatchDelayed, InvalidateDropsPendingDelayedPost)
{
    ensureApp();
    EventDispatch d;
    bool fired = false;
    d.postDelayed(20, [&] { fired = true; });
    d.invalidate();
    pump([] { return false; }, std::chrono::milliseconds(150));
    EXPECT_FALSE(fired);
}

TEST(EventDispatchDelayed, DestructionCancelsDelayedPost)
{
    ensureApp();
    bool fired = false;
    {
        EventDispatch d;
        d.postDelayed(20, [&] { fired = true; });
    }
    pump([] { return false; }, std::chrono::milliseconds(150));
    EXPECT_FALSE(fired);
}

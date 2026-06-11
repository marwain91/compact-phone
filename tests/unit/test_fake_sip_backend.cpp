// FakeSipBackend behavior: the scriptable in-memory backend that lets
// manager logic be tested without Asterisk. Event-delivery semantics
// (queued on the main thread, dropped after stop) are pinned here;
// backend-agnostic semantics live in the contract suite.
#include "core/sipbackend/fake/FakeSipBackend.h"

#include <QCoreApplication>
#include <QThread>

#include <gtest/gtest.h>

using namespace compactphone::sipbackend;

namespace {

QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_fake_sip_backend";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

struct RecordingListener : ISipBackendListener {
    std::vector<std::string> events;
    Qt::HANDLE lastThread = nullptr;

    void onRegState(AccountId id, bool active, int code,
                    const std::string &) override
    {
        lastThread = QThread::currentThreadId();
        events.push_back("reg:" + std::to_string(id) + ":"
                         + (active ? "up" : "down") + ":"
                         + std::to_string(code));
    }
};

AccountSettings testAccount()
{
    AccountSettings a;
    a.username = "100";
    a.domain = "example.test";
    a.password = "secret";
    return a;
}

class FakeBackendTest : public ::testing::Test {
protected:
    void SetUp() override
    {
        ensureApp();
        backend.setListener(&listener);
    }

    FakeSipBackend backend;
    RecordingListener listener;
};

} // namespace

TEST_F(FakeBackendTest, StartStopTogglesRunning)
{
    EXPECT_FALSE(backend.isRunning());
    EXPECT_TRUE(backend.start(EngineConfig{}));
    EXPECT_TRUE(backend.isRunning());
    backend.stop();
    EXPECT_FALSE(backend.isRunning());
}

TEST_F(FakeBackendTest, RegEventIsQueuedNotSynchronous)
{
    backend.start(EngineConfig{});
    const auto id = backend.addAccount(testAccount());
    ASSERT_NE(id, kInvalidAccountId);

    backend.simulateRegState(id, true, 200, "OK");
    // Contract rule 2: nothing is delivered synchronously.
    EXPECT_TRUE(listener.events.empty());

    pumpEvents();
    ASSERT_EQ(listener.events.size(), 1u);
    EXPECT_EQ(listener.events[0], "reg:" + std::to_string(id) + ":up:200");
    EXPECT_EQ(listener.lastThread, QThread::currentThreadId());
}

TEST_F(FakeBackendTest, NoEventsDeliveredAfterStop)
{
    backend.start(EngineConfig{});
    const auto id = backend.addAccount(testAccount());
    backend.simulateRegState(id, true, 200, "OK");

    backend.stop();   // queued event must be dropped, not delivered late
    pumpEvents();
    EXPECT_TRUE(listener.events.empty());
}

TEST_F(FakeBackendTest, ClearingListenerDropsPendingEvents)
{
    backend.start(EngineConfig{});
    const auto id = backend.addAccount(testAccount());
    backend.simulateRegState(id, true, 200, "OK");

    backend.setListener(nullptr);   // quiesce barrier
    pumpEvents();
    EXPECT_TRUE(listener.events.empty());
}

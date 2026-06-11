// FakeSipBackend behavior: the scriptable in-memory backend that lets
// manager logic be tested without Asterisk. Event-delivery semantics
// (queued on the main thread, dropped after stop) are pinned here;
// backend-agnostic semantics live in the contract suite.
#include "core/sipbackend/fake/FakeSipBackend.h"

#include <memory>

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
public:
    // m_app must be the first data member so QCoreApplication exists before
    // the FakeSipBackend QObject is constructed (members initialize in
    // declaration order).
    QCoreApplication *m_app = ensureApp();

protected:
    void SetUp() override
    {
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

TEST_F(FakeBackendTest, DestructionCancelsQueuedEvents)
{
    auto local = std::make_unique<FakeSipBackend>();
    local->setListener(&listener);
    local->start(EngineConfig{});
    const auto id = local->addAccount(testAccount());
    local->simulateRegState(id, true, 200, "OK");

    local.reset();   // must cancel the queued delivery, not crash
    pumpEvents();
    EXPECT_TRUE(listener.events.empty());
}

TEST_F(FakeBackendTest, SwappingListenerDropsEventsQueuedForPrevious)
{
    backend.start(EngineConfig{});
    const auto id = backend.addAccount(testAccount());
    backend.simulateRegState(id, true, 200, "OK");

    RecordingListener replacement;
    backend.setListener(&replacement);   // quiesce barrier
    pumpEvents();
    EXPECT_TRUE(listener.events.empty());
    EXPECT_TRUE(replacement.events.empty());

    // New events flow to the replacement.
    backend.simulateRegState(id, false, 503, "Service Unavailable");
    pumpEvents();
    ASSERT_EQ(replacement.events.size(), 1u);
    EXPECT_TRUE(listener.events.empty());
}

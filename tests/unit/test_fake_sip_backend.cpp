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
    void onIncomingCall(AccountId acc, CallId call,
                        const std::string &remoteUri,
                        const std::string &) override
    {
        events.push_back("incoming:" + std::to_string(acc) + ":"
                         + std::to_string(call) + ":" + remoteUri);
    }
    void onCallState(CallId id, CallState s, int code) override
    {
        events.push_back("call:" + std::to_string(id) + ":"
                         + std::to_string(static_cast<int>(s)) + ":"
                         + std::to_string(code));
    }
    void onMediaState(CallId id, bool active, bool held) override
    {
        events.push_back("media:" + std::to_string(id) + ":"
                         + (active ? "active" : "inactive") + ":"
                         + (held ? "held" : "unheld"));
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

std::string callEvent(CallId id, CallState s, int code)
{
    return "call:" + std::to_string(id) + ":"
        + std::to_string(static_cast<int>(s)) + ":" + std::to_string(code);
}

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

class FakeCallTest : public FakeBackendTest {
protected:
    void SetUp() override
    {
        FakeBackendTest::SetUp();
        backend.start(EngineConfig{});
        account = backend.addAccount(testAccount());
        pumpEvents();
        listener.events.clear();
    }

    AccountId account = kInvalidAccountId;
};

TEST_F(FakeCallTest, OutboundCallLifecycle)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    ASSERT_NE(id, kInvalidCallId);
    pumpEvents();
    EXPECT_EQ(listener.events,
              (std::vector<std::string>{
                  callEvent(id, CallState::Calling, 0)}));

    backend.simulateRemoteAnswer(id);
    pumpEvents();
    ASSERT_TRUE(backend.callExists(id));
    EXPECT_EQ(backend.callInfo(id).state, CallState::Confirmed);
    EXPECT_TRUE(backend.isMediaActive(id));
    EXPECT_TRUE(backend.isCaptureTransmitting(id));

    backend.hangup(id);
    pumpEvents();
    EXPECT_EQ(backend.callInfo(id).state, CallState::Disconnected);
    EXPECT_FALSE(backend.isMediaActive(id));
}

TEST_F(FakeCallTest, IncomingCallAnswerFlow)
{
    const auto id =
        backend.simulateIncomingCall(account, "sip:caller@example.test",
                                     "Caller");
    ASSERT_NE(id, kInvalidCallId);
    pumpEvents();
    ASSERT_FALSE(listener.events.empty());
    EXPECT_EQ(listener.events[0],
              "incoming:" + std::to_string(account) + ":"
                  + std::to_string(id) + ":sip:caller@example.test");

    EXPECT_TRUE(backend.answer(id));
    pumpEvents();
    EXPECT_EQ(backend.callInfo(id).state, CallState::Confirmed);
    EXPECT_TRUE(backend.isMediaActive(id));
}

TEST_F(FakeCallTest, DeclineReportsSipCode)
{
    const auto id =
        backend.simulateIncomingCall(account, "sip:x@example.test", "");
    pumpEvents();
    listener.events.clear();

    EXPECT_TRUE(backend.decline(id, 486));
    pumpEvents();
    EXPECT_EQ(listener.events,
              (std::vector<std::string>{
                  callEvent(id, CallState::Disconnected, 486)}));
}

TEST_F(FakeCallTest, HoldUnholdDrivesMediaState)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    backend.simulateRemoteAnswer(id);
    pumpEvents();

    EXPECT_TRUE(backend.hold(id));
    pumpEvents();
    EXPECT_TRUE(backend.callInfo(id).held);
    EXPECT_FALSE(backend.isMediaActive(id));   // LOCAL_HOLD parks media

    EXPECT_TRUE(backend.unhold(id));
    pumpEvents();
    EXPECT_FALSE(backend.callInfo(id).held);
    EXPECT_TRUE(backend.isMediaActive(id));
}

TEST_F(FakeCallTest, MuteDisconnectsCapture)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    backend.simulateRemoteAnswer(id);
    pumpEvents();

    EXPECT_TRUE(backend.setMuted(id, true));
    EXPECT_FALSE(backend.isCaptureTransmitting(id));
    EXPECT_TRUE(backend.isMediaActive(id));   // media stays up, mic off

    EXPECT_TRUE(backend.setMuted(id, false));
    EXPECT_TRUE(backend.isCaptureTransmitting(id));
}

TEST_F(FakeCallTest, OperationsOnConfirmedCallsOnly)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    pumpEvents();
    // Still in Calling: media-dependent ops must refuse.
    EXPECT_FALSE(backend.hold(id));
    EXPECT_FALSE(backend.setMuted(id, true));
    EXPECT_FALSE(backend.sendDtmf(id, "1", DtmfMethod::Rfc2833));
    EXPECT_FALSE(backend.startRecording(id, "/tmp/x.wav"));
}

TEST_F(FakeCallTest, ReleaseInvalidatesId)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    backend.simulateRemoteAnswer(id);
    pumpEvents();
    backend.hangup(id);
    pumpEvents();

    backend.releaseCall(id);
    EXPECT_FALSE(backend.callExists(id));
    EXPECT_FALSE(backend.hold(id));
    EXPECT_FALSE(backend.answer(id));
    EXPECT_FALSE(backend.isMediaActive(id));
}

TEST_F(FakeCallTest, DtmfDigitsAccumulate)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    backend.simulateRemoteAnswer(id);
    pumpEvents();

    EXPECT_TRUE(backend.sendDtmf(id, "12", DtmfMethod::Rfc2833));
    EXPECT_TRUE(backend.sendDtmf(id, "#", DtmfMethod::Info));
    EXPECT_EQ(backend.callInfo(id).dtmfSent, "12#");
}

TEST_F(FakeCallTest, RemoteHangupDeliversDisconnectWithCode)
{
    const auto id = backend.makeCall(account, "sip:200@example.test");
    backend.simulateRemoteAnswer(id);
    pumpEvents();
    listener.events.clear();

    backend.simulateRemoteHangup(id, 200);
    pumpEvents();
    EXPECT_EQ(listener.events,
              (std::vector<std::string>{
                  callEvent(id, CallState::Disconnected, 200)}));
    EXPECT_EQ(backend.callInfo(id).state, CallState::Disconnected);
}

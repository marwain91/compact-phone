#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}

std::string udpTarget(const std::string &extension)
{
    return "sip:" + extension + "@" + sipServer() + ";transport=udp";
}
} // namespace

class BlindTransferTest : public ::testing::Test {
protected:
    int argc = 1;
    char argv0[1] = {0};
    char *argv = argv0;
    std::unique_ptr<QCoreApplication> app;

    compactphone::sip::SipEngine engine;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    void SetUp() override
    {
        app = std::make_unique<QCoreApplication>(argc, &argv);
        ASSERT_TRUE(engine.start(0));
        ASSERT_TRUE(db.openInMemory());
    }
    void TearDown() override { engine.stop(); }
};

TEST_F(BlindTransferTest, TransfersOngoingCallToEchoExtension)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    auto mkAccount = [&](const std::string &user, const std::string &pwd, bool isDefault) {
        compactphone::sip::Account a;
        a.displayName = user;
        a.username = user;
        a.domain = sipServer();
        a.authUser = user;
        a.transport = compactphone::sip::Transport::Udp;
        a.enabled = true;
        a.isDefault = isDefault;
        a.registerOnStartup = true;
        return am.add(a, pwd);
    };

    std::mutex mtx;
    std::condition_variable cv;
    int registeredCount = 0;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx);
        if (s == compactphone::sip::RegistrationState::Registered) ++registeredCount;
        cv.notify_all();
    });

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return registeredCount >= 2; }));
    }

    compactphone::sip::CallManager cm(&am);
    int incomingId = -1;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    am.setOnIncomingCall([&](compactphone::sip::AccountId aid, int pjsipCallId) {
        const auto localCallId = cm.adoptIncomingCall(aid, pjsipCallId);
        std::lock_guard l(mtx);
        incomingId = localCallId;
        cv.notify_all();
    });
    cm.setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
        cv.notify_all();
    });

    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);

    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return incomingId > 0; }));
    }

    EXPECT_TRUE(cm.accept(incomingId));
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
            return observed[incomingId] == compactphone::sip::CallState::Confirmed &&
                   observed[outboundCall] == compactphone::sip::CallState::Confirmed;
        }));
    }
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.blindTransfer(incomingId, udpTarget("600")));

    // The post-transfer hangup is driven by the success NOTIFY through a
    // queued main-thread handler, so the wait must pump the event loop —
    // a blocking cv.wait_for would starve the very hangup it waits for.
    bool disconnected = false;
    const auto deadline = std::chrono::steady_clock::now() + 15s;
    while (!disconnected && std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        {
            std::lock_guard l(mtx);
            disconnected = observed[incomingId] ==
                           compactphone::sip::CallState::Disconnected;
        }
        std::this_thread::sleep_for(10ms);
    }
    ASSERT_TRUE(disconnected)
        << "transferred call was not hung up after the success NOTIFY";

    cm.hangup(outboundCall);
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(20ms);
    }
}

// A failed transfer must NOT tear down the live conversation. Asterisk
// rejects a REFER to an extension that does not exist in the dialplan, so
// the final transfer status is non-2xx — the originating call has to stay
// confirmed with media flowing, letting the user keep talking and try again.
TEST_F(BlindTransferTest, FailedTransferKeepsOriginalCallAlive)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    auto mkAccount = [&](const std::string &user, const std::string &pwd, bool isDefault) {
        compactphone::sip::Account a;
        a.displayName = user;
        a.username = user;
        a.domain = sipServer();
        a.authUser = user;
        a.transport = compactphone::sip::Transport::Udp;
        a.enabled = true;
        a.isDefault = isDefault;
        a.registerOnStartup = true;
        return am.add(a, pwd);
    };

    std::mutex mtx;
    std::condition_variable cv;
    int registeredCount = 0;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx);
        if (s == compactphone::sip::RegistrationState::Registered) ++registeredCount;
        cv.notify_all();
    });

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return registeredCount >= 2; }));
    }

    compactphone::sip::CallManager cm(&am);
    int incomingId = -1;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    am.setOnIncomingCall([&](compactphone::sip::AccountId aid, int pjsipCallId) {
        const auto localCallId = cm.adoptIncomingCall(aid, pjsipCallId);
        std::lock_guard l(mtx);
        incomingId = localCallId;
        cv.notify_all();
    });
    cm.setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
        cv.notify_all();
    });

    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return incomingId > 0; }));
    }
    EXPECT_TRUE(cm.accept(incomingId));
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
            return observed[incomingId] == compactphone::sip::CallState::Confirmed &&
                   observed[outboundCall] == compactphone::sip::CallState::Confirmed;
        }));
    }
    std::this_thread::sleep_for(500ms);

    // Extension 999 is not in the fixture dialplan — the transfer must fail.
    EXPECT_TRUE(cm.blindTransfer(incomingId, udpTarget("999")));

    // Pump the event loop through the REFER rejection / failure NOTIFY (the
    // transfer-status decision is queued to the main thread) and give a
    // wrongly-issued hangup time to surface as a state change.
    for (int i = 0; i < 100; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(20ms);
    }

    {
        std::lock_guard l(mtx);
        EXPECT_EQ(observed[incomingId], compactphone::sip::CallState::Confirmed)
            << "failed transfer must not tear down the live call";
    }
    EXPECT_TRUE(cm.isMediaActive(incomingId));

    cm.hangup(incomingId);
    cm.hangup(outboundCall);
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(20ms);
    }
}

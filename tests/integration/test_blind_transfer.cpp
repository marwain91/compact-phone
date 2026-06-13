#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

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
    // Observation state shared with PJSIP-thread callbacks: declared before
    // the managers (outlives every delivery); the map is only ever touched
    // under brief lock holds — never slept on through a condition_variable.
    std::atomic<int> incomingId{-1};
    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
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

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    auto &cm = smp.calls;
    // The incoming-call/state signals fire on the main thread from pumped
    // queued events; the connections sever automatically when cm is
    // destroyed, so no explicit quiesce is needed.
    QObject::connect(&cm, &compactphone::sip::CallManager::incomingCall,
                     [&incomingId](int id) { incomingId.store(id); });
    QObject::connect(&cm, &compactphone::sip::CallManager::callEvent, [&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
    });

    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);

    ASSERT_TRUE(pumpUntil([&] { return incomingId.load() > 0; }, 10s));

    EXPECT_TRUE(cm.accept(incomingId.load()));
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[incomingId.load()] == compactphone::sip::CallState::Confirmed &&
               observed[outboundCall] == compactphone::sip::CallState::Confirmed;
    }, 10s));
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.blindTransfer(incomingId.load(), udpTarget("600")));

    // The post-transfer hangup is driven by the success NOTIFY through a
    // queued main-thread handler, so the wait must pump the event loop —
    // a blocking wait would starve the very hangup it waits for.
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[incomingId.load()] ==
               compactphone::sip::CallState::Disconnected;
    }, 15s)) << "transferred call was not hung up after the success NOTIFY";

    cm.hangup(outboundCall);
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
}

// A failed transfer must NOT tear down the live conversation. Asterisk
// rejects a REFER to an extension that does not exist in the dialplan, so
// the final transfer status is non-2xx — the originating call has to stay
// confirmed with media flowing, letting the user keep talking and try again.
TEST_F(BlindTransferTest, FailedTransferKeepsOriginalCallAlive)
{
    std::atomic<int> incomingId{-1};
    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
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

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    auto &cm = smp.calls;
    QObject::connect(&cm, &compactphone::sip::CallManager::incomingCall,
                     [&incomingId](int id) { incomingId.store(id); });
    QObject::connect(&cm, &compactphone::sip::CallManager::callEvent, [&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
    });

    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] { return incomingId.load() > 0; }, 10s));
    EXPECT_TRUE(cm.accept(incomingId.load()));
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[incomingId.load()] == compactphone::sip::CallState::Confirmed &&
               observed[outboundCall] == compactphone::sip::CallState::Confirmed;
    }, 10s));
    std::this_thread::sleep_for(500ms);

    // Extension 999 is not in the fixture dialplan — the transfer must fail.
    EXPECT_TRUE(cm.blindTransfer(incomingId.load(), udpTarget("999")));

    // Pump the event loop through the REFER rejection / failure NOTIFY (the
    // transfer-status decision is queued to the main thread) and give a
    // wrongly-issued hangup time to surface as a state change.
    for (int i = 0; i < 100; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
        std::this_thread::sleep_for(20ms);
    }

    {
        std::lock_guard l(mtx);
        EXPECT_EQ(observed[incomingId.load()], compactphone::sip::CallState::Confirmed)
            << "failed transfer must not tear down the live call";
    }
    EXPECT_TRUE(cm.isMediaActive(incomingId.load()));

    cm.hangup(incomingId.load());
    cm.hangup(outboundCall);
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
}

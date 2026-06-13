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

class AttendedTransferTest : public ::testing::Test {
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

TEST_F(AttendedTransferTest, TransfersOriginalCallToConsultation)
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

    auto callA = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(callA, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] { return incomingId.load() > 0; }, 10s));
    EXPECT_TRUE(cm.accept(incomingId.load()));
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[incomingId.load()] == compactphone::sip::CallState::Confirmed &&
               observed[callA] == compactphone::sip::CallState::Confirmed;
    }, 10s));
    std::this_thread::sleep_for(500ms);

    auto callB = cm.makeCall(id1, udpTarget("600"));
    ASSERT_NE(callB, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callB] == compactphone::sip::CallState::Confirmed;
    }, 10s));

    EXPECT_TRUE(cm.attendedTransfer(callB, incomingId.load()));

    // The post-transfer hangup is driven by the success NOTIFY through a
    // queued main-thread handler, so the wait must pump the event loop —
    // a blocking wait would starve the very hangup it waits for.
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callB] == compactphone::sip::CallState::Disconnected &&
               observed[incomingId.load()] ==
                   compactphone::sip::CallState::Disconnected;
    }, 15s)) << "transfer legs were not hung up after the success NOTIFY";

    cm.hangup(callA);
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
}

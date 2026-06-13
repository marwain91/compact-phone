#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

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

// 3-way conference: 1001 places two calls (to 600 and another echo target),
// holds one, then mergeCalls to bridge them. We verify both legs return to
// Confirmed state with the held leg unheld and no exceptions thrown by
// PJSIP's audio-graph wiring. The audio-side correctness (cross-talk) is
// out of scope here; that would need an RTP capture analysis.
class ConferenceTest : public ::testing::Test {
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

TEST_F(ConferenceTest, MergesTwoEchoLegsIntoBridge)
{
    // Mutex-guarded map shared with the PJSIP-thread callback. Declared
    // before the managers (outlives every delivery) and only ever polled
    // under brief lock holds — never slept on through a condition_variable.
    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "Conf";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.isDefault = true;
    a.registerOnStartup = true;
    const auto accId = am.add(a, "compactphone1001");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        am, {accId}, compactphone::sip::RegistrationState::Registered, 10s));

    auto &cm = smp.calls;
    QObject::connect(&cm, &compactphone::sip::CallManager::callEvent, [&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed[id] = s;
    });

    // Two parallel echo-test calls. Asterisk's `demo-echotest` extension
    // accepts both independently, giving us two confirmed legs to merge.
    auto leg1 = cm.makeCall(udpTarget("600"));
    auto leg2 = cm.makeCall(udpTarget("600"));
    ASSERT_NE(leg1, compactphone::sip::kInvalidCallId);
    ASSERT_NE(leg2, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[leg1] == compactphone::sip::CallState::Confirmed &&
               observed[leg2] == compactphone::sip::CallState::Confirmed;
    }, 15s));

    // Hold leg2 so leg1 is the "active" foreground call. mergeCalls must
    // unhold leg2 and wire the audio bridge between leg1<->leg2.
    EXPECT_TRUE(cm.hold(leg2));
    std::this_thread::sleep_for(500ms);
    EXPECT_TRUE(cm.isHeld(leg2));

    EXPECT_TRUE(cm.mergeCalls(leg1, leg2));

    // After merge the held leg should be flagged as no-longer-held; the
    // deferred bridge-wiring runs ~400ms later so give it time.
    std::this_thread::sleep_for(1200ms);
    EXPECT_FALSE(cm.isHeld(leg2));
    {
        std::lock_guard l(mtx);
        EXPECT_EQ(observed[leg1], compactphone::sip::CallState::Confirmed);
        EXPECT_EQ(observed[leg2], compactphone::sip::CallState::Confirmed);
    }

    cm.hangup(leg1);
    cm.hangup(leg2);
    ASSERT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[leg1] == compactphone::sip::CallState::Disconnected &&
               observed[leg2] == compactphone::sip::CallState::Disconnected;
    }, 10s));

    // Pump the event loop until the 2.2s post-disconnect grace timer erases
    // both legs.
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
    EXPECT_EQ(cm.callCount(), 0u);
}

TEST_F(ConferenceTest, MergeRejectsNonConfirmedCalls)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto &cm = smp.calls;
    // No calls exist — both ids are bogus.
    EXPECT_FALSE(cm.mergeCalls(1, 2));
}

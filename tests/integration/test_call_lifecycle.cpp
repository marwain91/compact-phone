#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class CallLifecycleTest : public ::testing::Test {
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

TEST_F(CallLifecycleTest, MakeAndHangupTenTimes_CountReturnsToZero)
{
    // One atomic for all ten cycles (reset per iteration) — declared before
    // the manager so the address captured by the connected callStateChanged
    // slot outlives it; re-declaring inside the loop would dangle.
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "L"; a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    const auto accId = am.add(a, "compactphone1001");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        am, {accId}, compactphone::sip::RegistrationState::Registered, 10s));

    auto &cm = smp.calls;
    QObject::connect(&cm, &compactphone::sip::CallManager::callStateChanged, [&](compactphone::sip::CallState s) {
        observed.store(s);
    });
    for (int i = 0; i < 10; ++i) {
        observed.store(compactphone::sip::CallState::Idle);
        auto callId = cm.makeCall("sip:600@" + sipServer());
        ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
        ASSERT_TRUE(pumpUntil([&] {
            return observed.load() == compactphone::sip::CallState::Confirmed;
        }, 10s));
        cm.hangup(callId);
        ASSERT_TRUE(pumpUntil([&] {
            return observed.load() == compactphone::sip::CallState::Disconnected;
        }, 5s));
        // CallManager defers eraseCall by 2.2s so the UI can render a
        // "Call ended" lingering state; pump the event loop until the
        // grace timer fires before moving on.
        pumpUntil([&] { return cm.callCount() == 0; }, 4s);
    }

    EXPECT_EQ(cm.callCount(), 0u);
}

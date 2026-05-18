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

using namespace std::chrono_literals;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class HoldTest : public ::testing::Test {
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

TEST_F(HoldTest, HoldsAndUnholds)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "H"; a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    ASSERT_NE(am.add(a, "compactphone1001"), compactphone::sip::kInvalidAccountId);

    std::mutex mtx;
    std::condition_variable cv;
    compactphone::sip::RegistrationState rstate =
        compactphone::sip::RegistrationState::Unregistered;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx); rstate = s; cv.notify_all();
    });
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
            return rstate == compactphone::sip::RegistrationState::Registered;
        }));
    }

    compactphone::sip::CallManager cm(&am);
    compactphone::sip::CallState observed = compactphone::sip::CallState::Idle;
    cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed = s; cv.notify_all();
    });

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 15s, [&] {
            return observed == compactphone::sip::CallState::Confirmed;
        }));
    }

    EXPECT_TRUE(cm.hold(callId));
    std::this_thread::sleep_for(1s);
    EXPECT_TRUE(cm.isHeld(callId));

    EXPECT_TRUE(cm.unhold(callId));
    std::this_thread::sleep_for(1s);
    EXPECT_FALSE(cm.isHeld(callId));

    cm.hangup(callId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
}

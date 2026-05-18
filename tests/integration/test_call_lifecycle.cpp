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
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "L"; a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    const auto accId = am.add(a, "compactphone1001");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);

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
    for (int i = 0; i < 10; ++i) {
        compactphone::sip::CallState observed = compactphone::sip::CallState::Idle;
        cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
            std::lock_guard l(mtx); observed = s; cv.notify_all();
        });
        auto callId = cm.makeCall("sip:600@" + sipServer());
        ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
        {
            std::unique_lock l(mtx);
            ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
                return observed == compactphone::sip::CallState::Confirmed;
            }));
        }
        cm.hangup(callId);
        {
            std::unique_lock l(mtx);
            ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
                return observed == compactphone::sip::CallState::Disconnected;
            }));
        }
        // CallManager defers eraseCall by 2.2s so the UI can render a
        // "Call ended" lingering state; pump the event loop until the
        // grace timer fires before moving on.
        const auto deadline = std::chrono::steady_clock::now() + 4s;
        while (std::chrono::steady_clock::now() < deadline
               && cm.callCount() > 0) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
            std::this_thread::sleep_for(20ms);
        }
    }

    EXPECT_EQ(cm.callCount(), 0u);
}

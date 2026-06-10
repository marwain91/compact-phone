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
#include <functional>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}

// Polls cond every 100ms until it holds or timeout elapses. Pumps the Qt
// event loop each iteration so CallManager's queued invocations and retry
// timers (e.g. requestUnhold's deferred re-INVITE) actually run.
bool waitFor(const std::function<bool()> &cond,
             std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (!cond()) {
        if (std::chrono::steady_clock::now() >= deadline) return false;
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(100ms);
        QCoreApplication::processEvents();
    }
    return true;
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

    // Media goes live shortly after CONFIRMED; the capture link appearing in
    // the conference bridge is the "media is up" signal.
    ASSERT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 5s));
    ASSERT_TRUE(cm.isMediaActive(callId));

    // Hold sends a re-INVITE with a=sendonly. Only when Asterisk answers it
    // does pjsua move the media out of ACTIVE (LOCAL_HOLD) and drop the
    // capture link — asserting that pins the re-INVITE completing, not just
    // CallManager's bookkeeping flag flipping.
    EXPECT_TRUE(cm.hold(callId));
    EXPECT_TRUE(cm.isHeld(callId));
    EXPECT_TRUE(waitFor([&] { return !cm.isMediaActive(callId); }, 5s))
        << "hold re-INVITE never completed (media still ACTIVE)";
    EXPECT_TRUE(waitFor([&] { return !cm.isCaptureTransmitting(callId); }, 2s))
        << "mic still wired into a held call";

    // Unhold re-INVITEs back to sendrecv: media must return to ACTIVE and
    // the mic re-wire.
    EXPECT_TRUE(cm.unhold(callId));
    EXPECT_FALSE(cm.isHeld(callId));
    EXPECT_TRUE(waitFor([&] { return cm.isMediaActive(callId); }, 5s))
        << "unhold re-INVITE never completed (media not re-activated)";
    EXPECT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 2s))
        << "mic not re-wired after unhold";

    cm.hangup(callId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
}

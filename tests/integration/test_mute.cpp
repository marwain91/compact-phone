#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallEntry.h"
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

// Reads the call's live state through snapshot() (which queries PJSIP
// directly), so no Qt event loop is needed for the polling waits below.
compactphone::sip::CallState callState(
    const compactphone::sip::CallManager &cm, compactphone::sip::CallId id)
{
    for (const auto &e : cm.snapshot()) {
        if (e.id == id) return e.state;
    }
    return compactphone::sip::CallState::Disconnected;
}

// Registers the 1001 test account and waits for REGISTERED.
bool registerAccount(compactphone::sip::AccountsManager &am)
{
    compactphone::sip::Account a;
    a.displayName = "Mute";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.isDefault = true;
    a.registerOnStartup = true;
    if (am.add(a, "compactphone1001") == compactphone::sip::kInvalidAccountId) {
        return false;
    }
    std::mutex mtx;
    std::condition_variable cv;
    auto state = compactphone::sip::RegistrationState::Unregistered;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        { std::lock_guard l(mtx); state = s; }
        cv.notify_all();
    });
    bool ok = false;
    {
        std::unique_lock l(mtx);
        ok = cv.wait_for(l, 10s, [&] {
            return state == compactphone::sip::RegistrationState::Registered;
        });
    }
    // Drop the callback before mtx/cv go out of scope — a registration
    // refresh could fire it later against dangling captures.
    am.setOnRegistrationStateChanged({});
    return ok;
}
} // namespace

class MuteTest : public ::testing::Test {
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

// Mute then unmute the local mic on a live, confirmed call to the echo
// extension (600). Asserts through isCaptureTransmitting() — which inspects
// PJSIP's conference-bridge wiring — that "muted" means the capture device is
// actually disconnected from the call's media, not just that CallManager's
// bookkeeping bool flipped. Also pins idempotency of re-muting a muted call.
TEST_F(MuteTest, MutesAndUnmutesActiveCall)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    ASSERT_TRUE(registerAccount(am));
    compactphone::sip::CallManager cm(&am);

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(waitFor([&] {
        return callState(cm, callId)
               == compactphone::sip::CallState::Confirmed;
    }, 15s));
    ASSERT_FALSE(cm.isMuted(callId)); // a fresh call starts unmuted

    // Media can lag CONFIRMED; the capture link is wired on activation while
    // unmuted, so its appearance is the "media is live" signal.
    ASSERT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 5s));

    EXPECT_TRUE(cm.setMuted(callId, true));
    EXPECT_TRUE(cm.isMuted(callId));
    // The conference bridge applies (dis)connects on the audio-clock tick,
    // so the wiring view lags the transmit call by a few ms — poll it.
    EXPECT_TRUE(waitFor([&] {
        return !cm.isCaptureTransmitting(callId); // mic actually off
    }, 2s));

    // Idempotent: re-muting an already-muted live call still succeeds.
    EXPECT_TRUE(cm.setMuted(callId, true));
    EXPECT_TRUE(cm.isMuted(callId));

    EXPECT_TRUE(cm.setMuted(callId, false));
    EXPECT_FALSE(cm.isMuted(callId));
    EXPECT_TRUE(waitFor([&] {
        return cm.isCaptureTransmitting(callId); // mic actually back on
    }, 2s));

    cm.hangup(callId);
    EXPECT_TRUE(waitFor([&] {
        return callState(cm, callId)
               == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

// NOTE on the hold/unhold re-INVITE scenario: the production bug this file
// guards against (a re-INVITE re-firing onCallMediaState and re-opening the
// mic on a muted call) cannot be exercised end-to-end against this Asterisk
// fixture — locally-initiated re-INVITEs (hold/unhold) never receive a final
// response here, so media never renegotiates (see the backlog task on the
// fixture's re-INVITE handling; HoldTest has the same blind spot but only
// asserts bookkeeping). The handler branch is identical for first activation
// and re-activation, so MuteBeforeMediaIsAppliedOnActivation below pins the
// same code path: ACTIVE media event + muted state => capture stays unwired.

// Mute pressed before media is active must be applied when media comes up.
// Before the fix, setMuted recorded the desired state but returned false,
// and media activation wired the mic regardless — the UI showed muted while
// the mic went live.
TEST_F(MuteTest, MuteBeforeMediaIsAppliedOnActivation)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    ASSERT_TRUE(registerAccount(am));
    compactphone::sip::CallManager cm(&am);

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    // Mute immediately — the INVITE is still in flight, media is not active.
    EXPECT_TRUE(cm.setMuted(callId, true));
    EXPECT_TRUE(cm.isMuted(callId));

    ASSERT_TRUE(waitFor([&] {
        return callState(cm, callId)
               == compactphone::sip::CallState::Confirmed;
    }, 15s));

    // Media activates shortly after CONFIRMED; capture must never be wired.
    for (int i = 0; i < 20; ++i) {
        ASSERT_FALSE(cm.isCaptureTransmitting(callId))
            << "media activation ignored the recorded mute state";
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_TRUE(cm.isMuted(callId));

    // Unmute proves media is active (capture wires up promptly).
    EXPECT_TRUE(cm.setMuted(callId, false));
    EXPECT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 5s));

    cm.hangup(callId);
    EXPECT_TRUE(waitFor([&] {
        return callState(cm, callId)
               == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

// The unknown-id branches must return false / false rather than crash or report
// a phantom call as muted.
TEST_F(MuteTest, MuteRejectsUnknownCallId)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.setMuted(9999, true));
    EXPECT_FALSE(cm.isMuted(9999));
}

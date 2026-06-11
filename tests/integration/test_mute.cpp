#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallEntry.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <chrono>
#include <cstdlib>
#include <functional>
#include <thread>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}

// Polls cond until it holds or timeout elapses, pumping the Qt event loop so
// CallManager's queued invocations and retry timers (e.g. requestUnhold's
// deferred re-INVITE) actually run.
bool waitFor(const std::function<bool()> &cond,
             std::chrono::milliseconds timeout)
{
    return pumpUntil(cond, timeout);
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
    const auto id = am.add(a, "compactphone1001");
    if (id == compactphone::sip::kInvalidAccountId) return false;
    // Callback-free wait: polls the manager's atomic registration state, so
    // no lock or stack slot is ever shared with PJSIP threads.
    return waitForRegState(
        am, {id}, compactphone::sip::RegistrationState::Registered, 10s);
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

// The production bug this file guards against: a hold/unhold re-INVITE
// re-fires onCallMediaState, which re-wires capture on the renegotiated
// media — before the record-first fix it consulted stale mute state and
// silently re-opened the microphone on a muted call. isMediaActive() pins
// each re-INVITE actually completing (ACTIVE -> LOCAL_HOLD -> ACTIVE), so a
// fixture that swallows re-INVITEs fails the waits instead of letting the
// mute assertions pass vacuously.
TEST_F(MuteTest, MuteSurvivesHoldUnholdReinvite)
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
    ASSERT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 5s));

    ASSERT_TRUE(cm.setMuted(callId, true));
    ASSERT_TRUE(waitFor([&] {
        return !cm.isCaptureTransmitting(callId);
    }, 2s));

    // Hold: completed once media leaves ACTIVE (LOCAL_HOLD).
    ASSERT_TRUE(cm.hold(callId));
    ASSERT_TRUE(waitFor([&] { return !cm.isMediaActive(callId); }, 5s))
        << "hold re-INVITE never completed";

    // Unhold: media re-activates, onCallMediaState re-wires the call audio.
    ASSERT_TRUE(cm.unhold(callId));
    ASSERT_TRUE(waitFor([&] { return cm.isMediaActive(callId); }, 5s))
        << "unhold re-INVITE never completed";

    // The renegotiated media must come up with the mic still detached.
    for (int i = 0; i < 10; ++i) {
        ASSERT_FALSE(cm.isCaptureTransmitting(callId))
            << "unhold re-INVITE re-opened the microphone on a muted call";
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(100ms);
    }
    EXPECT_TRUE(cm.isMuted(callId));

    // Unmute lights the mic promptly — proves the call's media really is
    // live again rather than the mute checks passing against dead media.
    EXPECT_TRUE(cm.setMuted(callId, false));
    EXPECT_TRUE(waitFor([&] { return cm.isCaptureTransmitting(callId); }, 5s));

    cm.hangup(callId);
    EXPECT_TRUE(waitFor([&] {
        return callState(cm, callId)
               == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

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

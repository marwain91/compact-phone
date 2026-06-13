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
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "H"; a.username = "1001"; a.domain = sipServer();
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

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Confirmed;
    }, 15s));

    // Media goes live shortly after CONFIRMED; the capture link appearing in
    // the conference bridge is the "media is up" signal. The pumping wait
    // also runs CallManager's queued invocations and retry timers (e.g.
    // requestUnhold's deferred re-INVITE).
    ASSERT_TRUE(pumpUntil([&] { return cm.isCaptureTransmitting(callId); }, 5s));
    ASSERT_TRUE(cm.isMediaActive(callId));

    // Hold sends a re-INVITE with a=sendonly. Only when Asterisk answers it
    // does pjsua move the media out of ACTIVE (LOCAL_HOLD) and drop the
    // capture link — asserting that pins the re-INVITE completing, not just
    // CallManager's bookkeeping flag flipping.
    EXPECT_TRUE(cm.hold(callId));
    EXPECT_TRUE(cm.isHeld(callId));
    EXPECT_TRUE(pumpUntil([&] { return !cm.isMediaActive(callId); }, 5s))
        << "hold re-INVITE never completed (media still ACTIVE)";
    EXPECT_TRUE(pumpUntil([&] { return !cm.isCaptureTransmitting(callId); }, 2s))
        << "mic still wired into a held call";

    // Unhold re-INVITEs back to sendrecv: media must return to ACTIVE and
    // the mic re-wire.
    EXPECT_TRUE(cm.unhold(callId));
    EXPECT_FALSE(cm.isHeld(callId));
    EXPECT_TRUE(pumpUntil([&] { return cm.isMediaActive(callId); }, 5s))
        << "unhold re-INVITE never completed (media not re-activated)";
    EXPECT_TRUE(pumpUntil([&] { return cm.isCaptureTransmitting(callId); }, 2s))
        << "mic not re-wired after unhold";

    cm.hangup(callId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

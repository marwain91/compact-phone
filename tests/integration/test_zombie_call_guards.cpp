#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallEntry.h"
#include "core/CallManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;
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

// Registers the 1001 test account and waits for REGISTERED. Callback-free:
// polls the manager's atomic registration state, so no lock or stack slot
// is ever shared with PJSIP threads.
bool registerAccount(compactphone::sip::AccountsManager &am,
                     compactphone::sip::AccountId &outId)
{
    compactphone::sip::Account a;
    a.displayName = "Zombie";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.isDefault = true;
    a.registerOnStartup = true;
    outId = am.add(a, "compactphone1001");
    if (outId == compactphone::sip::kInvalidAccountId) return false;
    return waitForRegState(
        am, {outId}, compactphone::sip::RegistrationState::Registered, 10s);
}
} // namespace

class ZombieCallGuardsTest : public ::testing::Test {
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

// Operations on a call that has disconnected (and whose backend id has been
// released) must fail soft — false/empty/no-op, never a crash or a pj::Error
// escaping into the event loop. This covers the released-id path through
// CallManager and the adapter's unknown-id guards; the adapter's internal
// getInfo-throws-mid-teardown guards (moved verbatim from CallImpl) are
// additionally exercised nondeterministically by the ThreadStress suite.
//
// The synthetic adoption of a dead pjsua id (adoptIncomingCall) is gone with
// the queued-event model, so the call is disconnected the honest way: dialing
// an extension Asterisk rejects (no contact) so the dialog tears itself down,
// after which every CallManager operation runs against a released id.
TEST_F(ZombieCallGuardsTest, OperationsOnDisconnectedCallFailSoft)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto &cm = smp.calls;
    compactphone::sip::AccountId accountId = compactphone::sip::kInvalidAccountId;
    ASSERT_TRUE(registerAccount(am, accountId));

    // Dial an extension Asterisk rejects (no contact) so the call disconnects
    // on its own; 1009 is unrouted in the fixture dialplan (only 600/1001/1002
    // exist), so the dialog tears down and the backend id is released.
    const auto id = cm.makeCall(accountId, udpTarget("1009"));
    ASSERT_NE(id, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(compactphone::testsupport::pumpUntil(
        [&] { return cm.callCount() == 0; }, std::chrono::seconds(10)));

    EXPECT_FALSE(cm.setMuted(id, true));
    EXPECT_FALSE(cm.startRecording(id, "/tmp/zombie-call.wav"));
    EXPECT_FALSE(cm.playAudioFile(id, "/tmp/zombie-call.wav", false));
    EXPECT_FALSE(cm.isCaptureTransmitting(id));
    EXPECT_FALSE(cm.isMediaActive(id));
    EXPECT_FALSE(cm.hold(id));

    const auto stats = cm.streamStats(id);
    EXPECT_LT(stats.mos, 0.0); // empty stats, not garbage

    const auto snap = cm.snapshot();             // lingering entry only
    (void)snap;
    cm.hangup(id);                                // no-op, must not crash
    EXPECT_NE(cm.lastStatusCode(id), 0);          // disposition readable
}

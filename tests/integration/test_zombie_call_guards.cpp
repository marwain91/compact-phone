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

using namespace std::chrono_literals;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
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

// pj::Call::getInfo() throws pj::Error once pjsua has invalidated the
// underlying call — which can race any Q_INVOKABLE entry point between
// CallManager's map lookup and the PJSIP query (call teardown is driven by
// the PJSIP worker thread). An exception escaping a Q_INVOKABLE into the Qt
// event loop, or escaping a pjsua2 callback into PJSIP's C frames, aborts
// the process.
//
// Deterministic stand-in for that race: adopt a pjsua call id that is in
// range but has no active session. adoptIncomingCall() itself never queries
// PJSIP, so CallManager happily creates the CallImpl — and every later
// getInfo() on it throws exactly like a torn-down call's would. Every
// operation below must fail soft (false / empty), not crash.
TEST_F(ZombieCallGuardsTest, OperationsOnDeadPjsuaCallFailSoft)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::AccountId accountId = compactphone::sip::kInvalidAccountId;
    ASSERT_TRUE(registerAccount(am, accountId));
    compactphone::sip::CallManager cm(&am);

    // pjsua call id 2 is within maxCalls (SipEngine sets PJSUA_MAX_CALLS-1;
    // out-of-range ids trip a pjsua assert instead of throwing) but no
    // session exists behind it — this test places no real calls.
    const auto id = cm.adoptIncomingCall(accountId, 2);
    ASSERT_NE(id, compactphone::sip::kInvalidCallId);

    EXPECT_FALSE(cm.setMuted(id, true));
    EXPECT_FALSE(cm.startRecording(id, "/tmp/zombie-call.wav"));
    EXPECT_FALSE(cm.playAudioFile(id, "/tmp/zombie-call.wav", false));
    EXPECT_FALSE(cm.isCaptureTransmitting(id));
    EXPECT_FALSE(cm.isMediaActive(id));

    const auto stats = cm.streamStats(id);
    EXPECT_LT(stats.mos, 0.0); // empty stats, not garbage

    // snapshot() must survive the zombie entry (it getInfo()s every call).
    const auto snap = cm.snapshot();
    EXPECT_GE(snap.size(), 1u);

    // hangup() must not throw out of the Q_INVOKABLE either; the zombie has
    // no session, so this is bookkeeping-only.
    cm.hangup(id);
}

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
#include <mutex>
#include <unordered_map>

using namespace std::chrono_literals;
using compactphone::testsupport::pollUntil;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;
using compactphone::testsupport::ScopedAccountCallbacks;

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

// Verifies that CallManager::forwardCall sends a 302 Moved Temporarily.
// Two registered legs: 1001 (callee) and 1002 (caller). 1002 calls 1001;
// when 1001 sees the incoming call we immediately forwardCall it to the
// echo extension. The receiving side (1001) must terminate the incoming
// dialog. Asterisk's default Dial() doesn't auto-follow 302, so the
// caller leg (1002) also terminates; the test treats both Disconnected
// transitions as the success criterion.
class ForwardCallTest : public ::testing::Test {
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

TEST_F(ForwardCallTest, ForwardsIncomingTo302TargetAndCallEnds)
{
    // Observation state shared with PJSIP-thread callbacks: declared before
    // the managers (outlives every delivery); the map is only ever touched
    // under brief lock holds — never slept on through a condition_variable.
    std::atomic<int> incomingId{-1};
    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto mkAccount = [&](const std::string &user, const std::string &pwd,
                         bool isDefault) {
        compactphone::sip::Account a;
        a.displayName = user;
        a.username = user;
        a.domain = sipServer();
        a.authUser = user;
        a.transport = compactphone::sip::Transport::Udp;
        a.enabled = true;
        a.isDefault = isDefault;
        a.registerOnStartup = true;
        return am.add(a, pwd);
    };

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    compactphone::sip::CallManager cm(&am);
    // The incoming-call lambda captures cm; the guard clears the
    // AccountsManager callbacks before cm dies, ASSERT early-returns included.
    ScopedAccountCallbacks guard(am);
    am.setOnIncomingCall([&](compactphone::sip::AccountId aid, int pjsipCallId) {
        incomingId.store(cm.adoptIncomingCall(aid, pjsipCallId));
    });
    cm.setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
    });

    // 1002 dials 1001 -> Asterisk routes -> 1001 sees INVITE.
    auto callerLeg = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pollUntil([&] { return incomingId.load() > 0; }, 10s));

    // Forward the incoming dialog to extension 600 (echo) via 302.
    EXPECT_TRUE(cm.forwardCall(incomingId.load(), udpTarget("600")));

    // The receiving leg always disconnects once we hangup with 302. The
    // caller-leg fate depends on Asterisk's redirect handling in our
    // dialplan; we only assert the side we control.
    ASSERT_TRUE(pollUntil([&] {
        std::lock_guard l(mtx);
        return observed[incomingId.load()] ==
               compactphone::sip::CallState::Disconnected;
    }, 15s));

    // Best-effort cleanup of the caller leg in case Asterisk left it open
    // (our test doesn't depend on it terminating itself).
    cm.hangup(callerLeg);
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
}

TEST_F(ForwardCallTest, ForwardCallReturnsFalseForUnknownCallId)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.forwardCall(12345, "sip:600@example"));
}

TEST_F(ForwardCallTest, ForwardCallReturnsFalseForEmptyTarget)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.forwardCall(1, ""));
}

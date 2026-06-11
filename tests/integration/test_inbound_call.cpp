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
#include <thread>

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

class InboundCallTest : public ::testing::Test {
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

TEST_F(InboundCallTest, ReceivesAndAcceptsCallFromSecondAccount)
{
    // Lock-free observation state, declared before the managers so a late
    // PJSIP event delivered during their teardown can never write through a
    // dead stack slot.
    std::atomic<int> incomingCallId{-1};
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;

    auto mkAccount = [&](const std::string &user, const std::string &pwd, bool isDefault) {
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
    // The incoming-call lambda below captures cm; the guard clears every
    // AccountsManager callback before cm dies — including on ASSERT
    // early-returns, which skip cleanup written after the assertion.
    ScopedAccountCallbacks guard(am);
    am.setOnIncomingCall([&](compactphone::sip::AccountId aid, int pjsipCallId) {
        incomingCallId.store(cm.adoptIncomingCall(aid, pjsipCallId));
    });
    cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
        observed.store(s);
    });

    // 1002 dials 1001 — Asterisk routes via dialplan to PJSIP/1001.
    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);

    ASSERT_TRUE(pollUntil([&] { return incomingCallId.load() > 0; }, 10s));

    EXPECT_TRUE(cm.accept(incomingCallId.load()));

    ASSERT_TRUE(pollUntil([&] {
        return observed.load() == compactphone::sip::CallState::Confirmed;
    }, 10s));

    std::this_thread::sleep_for(1s);
    cm.hangup(incomingCallId.load());
    cm.hangup(outboundCall);

    // Drain the disconnects and the post-disconnect grace timer.
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);
}

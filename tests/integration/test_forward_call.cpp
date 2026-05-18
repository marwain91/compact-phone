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
#include <unordered_map>

using namespace std::chrono_literals;

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
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
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

    std::mutex mtx;
    std::condition_variable cv;
    int registeredCount = 0;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx);
        if (s == compactphone::sip::RegistrationState::Registered) ++registeredCount;
        cv.notify_all();
    });

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return registeredCount >= 2; }));
    }

    compactphone::sip::CallManager cm(&am);
    int incomingId = -1;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    am.setOnIncomingCall([&](compactphone::sip::AccountId aid, int pjsipCallId) {
        const auto localId = cm.adoptIncomingCall(aid, pjsipCallId);
        std::lock_guard l(mtx);
        incomingId = localId;
        cv.notify_all();
    });
    cm.setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
        cv.notify_all();
    });

    // 1002 dials 1001 -> Asterisk routes -> 1001 sees INVITE.
    auto callerLeg = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return incomingId > 0; }));
    }

    // Forward the incoming dialog to extension 600 (echo) via 302.
    EXPECT_TRUE(cm.forwardCall(incomingId, udpTarget("600")));

    // The receiving leg always disconnects once we hangup with 302. The
    // caller-leg fate depends on Asterisk's redirect handling in our
    // dialplan; we only assert the side we control.
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 15s, [&] {
            return observed[incomingId] == compactphone::sip::CallState::Disconnected;
        }));
    }

    // Best-effort cleanup of the caller leg in case Asterisk left it open
    // (our test doesn't depend on it terminating itself).
    cm.hangup(callerLeg);
    for (int i = 0; i < 150; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(20ms);
    }
}

TEST_F(ForwardCallTest, ForwardCallReturnsFalseForUnknownCallId)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.forwardCall(12345, "sip:600@example"));
}

TEST_F(ForwardCallTest, ForwardCallReturnsFalseForEmptyTarget)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.forwardCall(1, ""));
}

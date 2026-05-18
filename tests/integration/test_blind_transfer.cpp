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

class BlindTransferTest : public ::testing::Test {
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

TEST_F(BlindTransferTest, TransfersOngoingCallToEchoExtension)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
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
        const auto localCallId = cm.adoptIncomingCall(aid, pjsipCallId);
        std::lock_guard l(mtx);
        incomingId = localCallId;
        cv.notify_all();
    });
    cm.setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
        cv.notify_all();
    });

    auto outboundCall = cm.makeCall(id2, udpTarget("1001"));
    ASSERT_NE(outboundCall, compactphone::sip::kInvalidCallId);

    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return incomingId > 0; }));
    }

    EXPECT_TRUE(cm.accept(incomingId));
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
            return observed[incomingId] == compactphone::sip::CallState::Confirmed &&
                   observed[outboundCall] == compactphone::sip::CallState::Confirmed;
        }));
    }
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.blindTransfer(incomingId, udpTarget("600")));

    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 15s, [&] {
            return observed[incomingId] == compactphone::sip::CallState::Disconnected;
        }));
    }

    cm.hangup(outboundCall);
    for (int i = 0; i < 30; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(20ms);
    }
}

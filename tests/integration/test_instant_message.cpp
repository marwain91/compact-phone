#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <chrono>
#include <cstdlib>
#include <mutex>

using namespace std::chrono_literals;
using compactphone::testsupport::pollUntil;
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

class InstantMessageTest : public ::testing::Test {
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

TEST_F(InstantMessageTest, AccountToAccountRoundTripDeliversBody)
{
    // Observation state shared with the PJSIP-thread callback: declared
    // before the manager (outlives every delivery); the strings are only
    // ever touched under brief lock holds — never slept on through a
    // condition_variable.
    struct Received {
        compactphone::sip::AccountId account = compactphone::sip::kInvalidAccountId;
        std::string from;
        std::string body;
    };
    std::mutex mtx;
    Received got;
    bool gotMessage = false;

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

    ScopedAccountCallbacks guard(am);
    am.setOnInstantMessage([&](compactphone::sip::AccountId acc,
                               const std::string &from,
                               const std::string &body) {
        std::lock_guard l(mtx);
        got = {acc, from, body};
        gotMessage = true;
    });

    // 1002 -> 1001 (Asterisk routes the out-of-dialog MESSAGE based on
    // the request URI; both endpoints are registered so it lands at
    // 1001's AccountImpl::onInstantMessage).
    const std::string body = "Hello from integration test";
    EXPECT_TRUE(am.sendInstantMessage(id2, udpTarget("1001"), body));

    ASSERT_TRUE(pollUntil([&] {
        std::lock_guard l(mtx);
        return gotMessage;
    }, 10s));
    std::lock_guard l(mtx);
    EXPECT_EQ(got.account, id1);
    EXPECT_EQ(got.body, body);
    EXPECT_NE(got.from.find("1002"), std::string::npos)
        << "Sender URI " << got.from << " does not contain 1002";
}

TEST_F(InstantMessageTest, SendFailsWhenAccountUnknown)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    EXPECT_FALSE(am.sendInstantMessage(9999, "sip:1001@x", "hi"));
}

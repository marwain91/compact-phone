#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <chrono>
#include <cstdlib>
#include <string>

using namespace std::chrono_literals;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class RegisterUdpTest : public ::testing::Test {
protected:
    compactphone::sip::SipEngine engine;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    void SetUp() override
    {
        ASSERT_TRUE(engine.start(0));
        ASSERT_TRUE(db.openInMemory());
    }
    void TearDown() override { engine.stop(); }
};

TEST_F(RegisterUdpTest, RegistersExtension1001)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &mgr = smp.manager;

    compactphone::sip::Account a;
    a.displayName = "Test 1001";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr.add(a, "compactphone1001");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        mgr, {id}, compactphone::sip::RegistrationState::Registered, 10s));

    EXPECT_EQ(mgr.stateOf(id), compactphone::sip::RegistrationState::Registered);
    mgr.remove(id);
}

TEST_F(RegisterUdpTest, RejectsInvalidPassword)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &mgr = smp.manager;

    compactphone::sip::Account a;
    a.displayName = "Bad Password";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr.add(a, "wrong-password");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        mgr, {id}, compactphone::sip::RegistrationState::Failed, 10s));

    EXPECT_EQ(mgr.stateOf(id), compactphone::sip::RegistrationState::Failed);
    mgr.remove(id);
}

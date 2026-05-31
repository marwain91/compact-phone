#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>

using namespace std::chrono_literals;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5061";
}
} // namespace

class RegisterTlsTest : public ::testing::Test {
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

TEST_F(RegisterTlsTest, RegistersOverTlsWithSelfSignedCert)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    std::mutex mtx;
    std::condition_variable cv;
    compactphone::sip::RegistrationState observed =
        compactphone::sip::RegistrationState::Unregistered;
    mgr.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx); observed = s; cv.notify_all();
    });

    compactphone::sip::Account a;
    a.displayName = "Test TLS";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Tls;
    a.srtpMode = compactphone::sip::SrtpMode::Optional;
    a.allowUntrustedCert = true;
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr.add(a, "compactphone1001");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 20s, [&] {
            return observed == compactphone::sip::RegistrationState::Registered;
        }));
    }

    mgr.remove(id);
}

// Fail-closed guarantee: an account that does NOT opt into allowUntrustedCert
// must reject the self-signed Asterisk certificate and never reach Registered.
// This is the security regression test for the TLS-verify default.
TEST_F(RegisterTlsTest, RejectsSelfSignedCertWhenVerificationRequired)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    std::mutex mtx;
    std::condition_variable cv;
    bool sawRegistered = false;
    bool sawFailed = false;
    mgr.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx);
        if (s == compactphone::sip::RegistrationState::Registered) sawRegistered = true;
        if (s == compactphone::sip::RegistrationState::Failed) sawFailed = true;
        cv.notify_all();
    });

    compactphone::sip::Account a;
    a.displayName = "Test TLS verify";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Tls;
    a.srtpMode = compactphone::sip::SrtpMode::Optional;
    a.allowUntrustedCert = false; // require verification
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr.add(a, "compactphone1001");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    {
        // The handshake should fail; wait for a Failed state and confirm we
        // never observed a successful registration.
        std::unique_lock l(mtx);
        cv.wait_for(l, 20s, [&] { return sawFailed; });
        EXPECT_TRUE(sawFailed);
        EXPECT_FALSE(sawRegistered);
    }

    mgr.remove(id);
}

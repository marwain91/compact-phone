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
// PEM of the CA that signed the test Asterisk's TLS cert. The Asterisk cert is
// self-signed (CN=asterisk), so the cert file IS its own trust anchor.
std::string caCertFile()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_CA")) return env;
    return "/workspace/tests/integration/docker/tls/server.crt";
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

// Verify-AND-accept: with the server's CA in the trust store, a verifying
// account (allowUntrustedCert=false) must ACCEPT the cert and register. This
// pins the path the fail-closed default needs to actually work against a
// legitimately-signed server (issue #68) — distinct from the negative test
// above, which only proves an UNtrusted cert is rejected. Uses its own engine
// so the CA bundle can be set before start().
TEST(RegisterTlsVerifiedTest, AcceptsTrustedCaSignedCertWithVerificationOn)
{
    compactphone::sip::SipEngine engine;
    engine.setCaCertFile(caCertFile());
    ASSERT_TRUE(engine.start(0));
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::platform::MemoryKeychain kc;
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);

    std::mutex mtx;
    std::condition_variable cv;
    compactphone::sip::RegistrationState observed =
        compactphone::sip::RegistrationState::Unregistered;
    mgr.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx); observed = s; cv.notify_all();
    });

    compactphone::sip::Account a;
    a.displayName = "Test TLS verified";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Tls;
    a.srtpMode = compactphone::sip::SrtpMode::Optional;
    a.allowUntrustedCert = false; // require verification — and it must SUCCEED
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
    engine.stop();
}

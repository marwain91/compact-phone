#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "core/sipbackend/pjsip/PjsipBackend.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

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

TEST_F(RegisterTlsTest, RegistersOverTlsWithSelfSignedCert)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &mgr = smp.manager;

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

    ASSERT_TRUE(waitForRegState(
        mgr, {id}, compactphone::sip::RegistrationState::Registered, 20s));

    mgr.remove(id);
}

// Fail-closed guarantee: an account that does NOT opt into allowUntrustedCert
// must reject the self-signed Asterisk certificate and never reach Registered.
// This is the security regression test for the TLS-verify default.
TEST_F(RegisterTlsTest, RejectsSelfSignedCertWhenVerificationRequired)
{
    // Transition history, not just current state: the test must prove
    // Registered was NEVER reached, which a current-state poll could miss
    // between samples. Lock-free atomics — declared before mgr so they
    // outlive every callback delivery, including ~AccountsManager's
    // unregister events.
    std::atomic<bool> sawRegistered{false};
    std::atomic<bool> sawFailed{false};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &mgr = smp.manager;
    QObject::connect(&mgr, &compactphone::sip::AccountsManager::registrationStateChanged,
                     [&](compactphone::sip::AccountId,
                         compactphone::sip::RegistrationState s) {
        if (s == compactphone::sip::RegistrationState::Registered) {
            sawRegistered.store(true);
        }
        if (s == compactphone::sip::RegistrationState::Failed) {
            sawFailed.store(true);
        }
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

    // The handshake should fail; wait for a Failed state and confirm we
    // never observed a successful registration. The callback fires via the
    // queued main-thread listener event — pump the loop.
    pumpUntil([&] { return sawFailed.load(); }, 20s);
    EXPECT_TRUE(sawFailed.load());
    EXPECT_FALSE(sawRegistered.load());

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
    int argc = 1;
    char argv0[] = "test";
    char *argv[] = {argv0};
    std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance())
        app = std::make_unique<QCoreApplication>(argc, argv);

    compactphone::sip::SipEngine engine;
    engine.setCaCertFile(caCertFile());
    ASSERT_TRUE(engine.start(0));
    compactphone::persistence::Database db;
    ASSERT_TRUE(db.openInMemory());
    compactphone::platform::MemoryKeychain kc;
    compactphone::sipbackend::PjsipBackend backend(&engine);
    compactphone::sip::AccountsManager mgr(&backend, &db, &kc);
    backend.setListener(&mgr);
    mgr.registerStartupAccounts(); // DB empty here; mirrors buildCoreSipGraph order

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

    ASSERT_TRUE(waitForRegState(
        mgr, {id}, compactphone::sip::RegistrationState::Registered, 20s));

    mgr.remove(id);
    backend.setListener(nullptr);
    engine.stop();
}

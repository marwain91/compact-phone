// AccountsManager field-update, persistence, and DB round-trip tests.
//
// Phase-2 rewrite: AccountsManager now drives registration through
// ISipBackend instead of directly using pjsua2. Tests construct it with a
// STARTED FakeSipBackend (so registerAccount() can succeed when enabled=true)
// plus nullptr pjsipBridge (no PJSIP-specific features exercised here).
//
// Where the old tests depended on pjsua2 being live (e.g. asserting on
// pjAccountFor, waiting for PJSIP reg events), they are replaced by:
//   - fake.commandLog() assertions for addAccount/removeAccount sequences
//   - fake.simulateRegState() + QCoreApplication::processEvents() for
//     stateOf/lastRegErrorOf through the real listener path

#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/sipbackend/fake/FakeSipBackend.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <QCoreApplication>
#include <QTemporaryDir>

namespace {

QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_accounts_manager_update";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

void pumpEvents()
{
    QCoreApplication::processEvents(QEventLoop::AllEvents);
}

} // namespace

class AccountsManagerUpdateTest : public ::testing::Test {
protected:
    compactphone::sipbackend::FakeSipBackend fake;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    void SetUp() override
    {
        ensureApp();
        ASSERT_TRUE(fake.start({}));
        ASSERT_TRUE(db.openInMemory());
    }
    void TearDown() override
    {
        fake.stop();
    }

    // Build a manager and wire the listener. Caller must keep the manager
    // alive while events may arrive — see CoreSipGraph.h wiring contract.
    std::unique_ptr<compactphone::sip::AccountsManager> makeManager()
    {
        auto mgr = std::make_unique<compactphone::sip::AccountsManager>(
            &fake, nullptr, &db, &kc);
        fake.setListener(mgr.get());
        return mgr;
    }
};

// Every other test reopens an in-memory DB on the *same live connection*, which
// never exercises a cold reopen of an on-disk database — the path that broke
// when loadFromDatabase's SELECT referenced a column missing from the persisted
// schema (the migration-collapse bug only the running app surfaced). Open a
// real file, write an account, then reopen the file with a fresh connection +
// manager and confirm loadFromDatabase round-trips it (including provider).
TEST_F(AccountsManagerUpdateTest, AccountSurvivesColdFileReopen)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("accounts.db").toStdString();

    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        compactphone::persistence::Database fileDb;
        ASSERT_TRUE(fileDb.open(path));
        auto mgr = std::make_unique<compactphone::sip::AccountsManager>(
            &fake, nullptr, &fileDb, &kc);
        fake.setListener(mgr.get());
        compactphone::sip::Account a;
        a.displayName = "Persisted";
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.provider = "daktela";
        a.enabled = false; // avoid a registration attempt
        id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        fake.setListener(nullptr);
    }

    // Cold reopen: new connection to the same file, new manager → loadFromDatabase.
    {
        compactphone::persistence::Database fileDb;
        ASSERT_TRUE(fileDb.open(path));
        auto mgr = std::make_unique<compactphone::sip::AccountsManager>(
            &fake, nullptr, &fileDb, &kc);
        fake.setListener(mgr.get());
        const auto loaded = mgr->find(id);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_EQ(loaded->displayName, "Persisted");
        EXPECT_EQ(loaded->provider, "daktela");
        fake.setListener(nullptr);
    }
}

TEST_F(AccountsManagerUpdateTest, UpdatePersistsChangedFields)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.displayName = "Original"; a.username = "1001";
    a.domain = "asterisk:5060"; a.enabled = false;
    const auto id = mgr->add(a, "secret");

    auto edited = mgr->find(id).value();
    edited.displayName = "Renamed";
    edited.dtmfMethod = compactphone::sip::DtmfMethod::Info;
    edited.proxy = "proxy.example.com:5060";
    ASSERT_TRUE(mgr->update(edited));

    auto fresh = mgr->find(id).value();
    EXPECT_EQ(fresh.displayName, "Renamed");
    EXPECT_EQ(fresh.dtmfMethod, compactphone::sip::DtmfMethod::Info);
    EXPECT_EQ(fresh.proxy, "proxy.example.com:5060");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, AddPersistsFullAccountAndReloadsFromDatabase)
{
    {
        auto mgr = makeManager();
        compactphone::sip::Account a;
        a.label = "Office";
        a.displayName = "Agent";
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.authUser = "auth-1001";
        a.transport = compactphone::sip::Transport::Tls;
        a.proxy = "proxy.example.com";
        a.stunServer = "stun.example.com";
        a.publicAddress = "203.0.113.5";
        a.voicemailNumber = "*97";
        a.registerOnStartup = false;
        a.registerIntervalSec = 180;
        a.keepaliveIntervalSec = 25;
        a.sessionTimersEnabled = false;
        a.publishPresenceEnabled = true;
        a.iceEnabled = true;
        a.hideCallerId = true;
        a.srtpMode = compactphone::sip::SrtpMode::Required;
        a.allowUntrustedCert = true;
        a.dtmfMethod = compactphone::sip::DtmfMethod::Info;
        a.enabled = false;
        a.isDefault = true;

        const auto id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        ASSERT_TRUE(kc.get(mgr->passwordRefFor(id)).has_value());
        fake.setListener(nullptr);
    }

    auto mgr2 = makeManager();
    const auto accounts = mgr2->list();
    ASSERT_EQ(accounts.size(), 1u);
    const auto &loaded = accounts[0];
    EXPECT_EQ(loaded.label, "Office");
    EXPECT_EQ(loaded.displayName, "Agent");
    EXPECT_EQ(loaded.username, "1001");
    EXPECT_EQ(loaded.domain, "pbx.example.com");
    EXPECT_EQ(loaded.authUser, "auth-1001");
    EXPECT_EQ(loaded.transport, compactphone::sip::Transport::Tls);
    EXPECT_EQ(loaded.proxy, "proxy.example.com");
    EXPECT_EQ(loaded.stunServer, "stun.example.com");
    EXPECT_EQ(loaded.publicAddress, "203.0.113.5");
    EXPECT_EQ(loaded.voicemailNumber, "*97");
    EXPECT_FALSE(loaded.registerOnStartup);
    EXPECT_EQ(loaded.registerIntervalSec, 180);
    EXPECT_EQ(loaded.keepaliveIntervalSec, 25);
    EXPECT_FALSE(loaded.sessionTimersEnabled);
    EXPECT_TRUE(loaded.publishPresenceEnabled);
    EXPECT_TRUE(loaded.iceEnabled);
    EXPECT_TRUE(loaded.hideCallerId);
    EXPECT_EQ(loaded.srtpMode, compactphone::sip::SrtpMode::Required);
    EXPECT_TRUE(loaded.allowUntrustedCert);
    EXPECT_EQ(loaded.dtmfMethod, compactphone::sip::DtmfMethod::Info);
    EXPECT_FALSE(loaded.enabled);
    EXPECT_TRUE(loaded.isDefault);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, UpdateUnknownIdReturnsFalse)
{
    auto mgr = makeManager();
    compactphone::sip::Account ghost;
    ghost.id = 9999;
    EXPECT_FALSE(mgr->update(ghost));
    EXPECT_FALSE(mgr->remove(ghost.id));
    EXPECT_FALSE(mgr->setDefault(ghost.id));
    EXPECT_EQ(mgr->stateOf(ghost.id), compactphone::sip::RegistrationState::Unregistered);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, SetDefaultFlipsFlagAndClearsOthers)
{
    auto mgr = makeManager();
    compactphone::sip::Account a, b;
    a.displayName = "A"; a.username = "u1"; a.domain = "d"; a.enabled = false;
    a.isDefault = true;
    b.displayName = "B"; b.username = "u2"; b.domain = "d"; b.enabled = false;
    const auto idA = mgr->add(a, "pa");
    const auto idB = mgr->add(b, "pb");

    EXPECT_EQ(mgr->defaultAccountId(), compactphone::sip::kInvalidAccountId);
    // (Both disabled, no default returned.)

    ASSERT_TRUE(mgr->setDefault(idB));
    auto av = mgr->find(idA).value();
    auto bv = mgr->find(idB).value();
    EXPECT_FALSE(av.isDefault);
    EXPECT_TRUE(bv.isDefault);
    fake.setListener(nullptr);
}

// Test that enable/disable cycles issue the expected addAccount/removeAccount
// commands to the backend. Verifies the ISipBackend is actually driven.
TEST_F(AccountsManagerUpdateTest, SetEnabledIssuesBackendCommands)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = false;
    a.registerOnStartup = false;
    const auto id = mgr->add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    // Initially disabled: no addAccount should have been issued.
    const auto &log = fake.commandLog();
    EXPECT_TRUE(std::none_of(log.begin(), log.end(),
        [](const std::string &s){ return s.rfind("addAccount:", 0) == 0; }));

    // Enable: should issue addAccount.
    ASSERT_TRUE(mgr->setEnabled(id, true));
    const auto &log2 = fake.commandLog();
    EXPECT_TRUE(std::any_of(log2.begin(), log2.end(),
        [](const std::string &s){ return s.rfind("addAccount:", 0) == 0; }));

    // Disable: should issue removeAccount.
    ASSERT_TRUE(mgr->setEnabled(id, false));
    const auto &log3 = fake.commandLog();
    EXPECT_TRUE(std::any_of(log3.begin(), log3.end(),
        [](const std::string &s){ return s.rfind("removeAccount:", 0) == 0; }));

    fake.setListener(nullptr);
}

// Test that stateOf/lastRegErrorOf reflect listener-driven state changes.
TEST_F(AccountsManagerUpdateTest, ListenerDrivenRegStateIsObservable)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr->add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    // Find the backend id minted by addAccount from the command log.
    // addAccount entries have the form "addAccount:<backendId>:<username>".
    compactphone::sipbackend::AccountId backendId =
        compactphone::sipbackend::kInvalidAccountId;
    for (const auto &entry : fake.commandLog()) {
        if (entry.rfind("addAccount:", 0) == 0) {
            // Parse "addAccount:<id>:<username>"
            const auto first = entry.find(':', 11);
            if (first != std::string::npos) {
                backendId = std::stoi(entry.substr(11, first - 11));
            }
            break;
        }
    }
    ASSERT_NE(backendId, compactphone::sipbackend::kInvalidAccountId)
        << "No addAccount entry in command log";

    // Simulate a successful registration event from the fake backend.
    fake.simulateRegState(backendId, /*regActive=*/true, /*sipCode=*/200,
                          "OK");
    pumpEvents();

    EXPECT_EQ(mgr->stateOf(id), compactphone::sip::RegistrationState::Registered);
    EXPECT_TRUE(mgr->lastRegErrorOf(id).empty());

    // Simulate a failure.
    fake.simulateRegState(backendId, /*regActive=*/false, /*sipCode=*/401,
                          "Unauthorized");
    pumpEvents();

    EXPECT_EQ(mgr->stateOf(id), compactphone::sip::RegistrationState::Failed);
    EXPECT_EQ(mgr->lastRegErrorOf(id).code, 401);

    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, UpdateDisablesLiveAccount)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.displayName = "Live";
    a.username = "1001";
    a.authUser = "1001";
    a.domain = "127.0.0.1:9";
    a.enabled = true;
    a.registerOnStartup = false;

    const auto id = mgr->add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(mgr->registerAccount(id));

    // After registerAccount, backend should have an addAccount entry.
    EXPECT_TRUE(std::any_of(fake.commandLog().begin(), fake.commandLog().end(),
        [](const std::string &s){ return s.rfind("addAccount:", 0) == 0; }));

    auto edited = mgr->find(id).value();
    edited.enabled = false;
    ASSERT_TRUE(mgr->update(edited));

    // After disabling, pjAccountFor returns nullptr (no backend account).
    EXPECT_EQ(mgr->pjAccountFor(id), nullptr);
    EXPECT_EQ(mgr->stateOf(id), compactphone::sip::RegistrationState::Unregistered);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, RemoveDeletesDatabaseRowAndPassword)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.displayName = "Remove";
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = false;
    const auto id = mgr->add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    const auto ref = mgr->passwordRefFor(id);
    ASSERT_TRUE(kc.get(ref).has_value());

    ASSERT_TRUE(mgr->remove(id));

    EXPECT_FALSE(kc.get(ref).has_value());
    EXPECT_FALSE(mgr->find(id).has_value());

    fake.setListener(nullptr);
    // Reload with fresh manager.
    auto mgr2 = makeManager();
    EXPECT_FALSE(mgr2->find(id).has_value());
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, AddRoundTripsZrtpCodecsAndAuthRealm)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        auto mgr = makeManager();
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        a.zrtpEnabled = true;
        a.codecs = "opus,alaw";
        a.authRealm = "realm.example";
        a.provider = "daktela";
        id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        fake.setListener(nullptr);
    }

    auto mgr2 = makeManager();
    const auto loaded = mgr2->find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->zrtpEnabled);
    EXPECT_EQ(loaded->codecs, "opus,alaw");
    EXPECT_EQ(loaded->authRealm, "realm.example");
    EXPECT_EQ(loaded->provider, "daktela");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, UpdateRoundTripsSecurityAndTransportFields)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        auto mgr = makeManager();
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        a.transport = compactphone::sip::Transport::Udp;
        a.srtpMode = compactphone::sip::SrtpMode::Disabled;
        a.allowUntrustedCert = false;
        a.zrtpEnabled = false;
        a.codecs = "ulaw";
        a.authRealm = "old.realm";
        a.provider = "";
        id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        auto edited = mgr->find(id).value();
        edited.transport = compactphone::sip::Transport::Tls;
        edited.srtpMode = compactphone::sip::SrtpMode::Required;
        edited.allowUntrustedCert = true;
        edited.zrtpEnabled = true;
        edited.codecs = "opus,alaw";
        edited.authRealm = "new.realm";
        edited.provider = "daktela";
        ASSERT_TRUE(mgr->update(edited));
        fake.setListener(nullptr);
    }

    auto mgr2 = makeManager();
    const auto loaded = mgr2->find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->transport, compactphone::sip::Transport::Tls);
    EXPECT_EQ(loaded->srtpMode, compactphone::sip::SrtpMode::Required);
    EXPECT_TRUE(loaded->allowUntrustedCert);
    EXPECT_TRUE(loaded->zrtpEnabled);
    EXPECT_EQ(loaded->codecs, "opus,alaw");
    EXPECT_EQ(loaded->authRealm, "new.realm");
    EXPECT_EQ(loaded->provider, "daktela");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, SetEnabledPersistsFlagAcrossReload)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        auto mgr = makeManager();
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "127.0.0.1:9";
        a.enabled = false;
        a.registerOnStartup = false;
        id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        ASSERT_TRUE(mgr->setEnabled(id, true));
        fake.setListener(nullptr);
    }

    {
        auto mgr2 = makeManager();
        const auto loaded = mgr2->find(id);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_TRUE(loaded->enabled);

        ASSERT_TRUE(mgr2->setEnabled(id, false));
        fake.setListener(nullptr);
    }

    auto mgr3 = makeManager();
    const auto loaded2 = mgr3->find(id);
    ASSERT_TRUE(loaded2.has_value());
    EXPECT_FALSE(loaded2->enabled);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, SetEnabledNoOpAndUnknownId)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        auto mgr = makeManager();
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        id = mgr->add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        EXPECT_TRUE(mgr->setEnabled(id, false));
        EXPECT_FALSE(mgr->find(id).value().enabled);

        EXPECT_FALSE(mgr->setEnabled(9999, true));
        fake.setListener(nullptr);
    }

    auto mgr2 = makeManager();
    const auto loaded = mgr2->find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->enabled);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerUpdateTest, SetDefaultPersistsSingleDefaultInvariant)
{
    compactphone::sip::AccountId idA = compactphone::sip::kInvalidAccountId;
    compactphone::sip::AccountId idB = compactphone::sip::kInvalidAccountId;
    {
        auto mgr = makeManager();
        compactphone::sip::Account a, b;
        a.username = "u1"; a.domain = "d"; a.enabled = false; a.isDefault = true;
        b.username = "u2"; b.domain = "d"; b.enabled = false;
        idA = mgr->add(a, "pa");
        idB = mgr->add(b, "pb");
        ASSERT_NE(idA, compactphone::sip::kInvalidAccountId);
        ASSERT_NE(idB, compactphone::sip::kInvalidAccountId);

        ASSERT_TRUE(mgr->setDefault(idB));
        fake.setListener(nullptr);
    }

    auto mgr2 = makeManager();
    const auto accounts = mgr2->list();
    ASSERT_EQ(accounts.size(), 2u);
    int defaults = 0;
    compactphone::sip::AccountId defaultId = compactphone::sip::kInvalidAccountId;
    for (const auto &acc : accounts) {
        if (acc.isDefault) { ++defaults; defaultId = acc.id; }
    }
    EXPECT_EQ(defaults, 1);
    EXPECT_EQ(defaultId, idB);
    fake.setListener(nullptr);
}

// Pin the observable: lastRegErrorOf returns empty once an account is
// unregistered (setEnabled false), even if it previously failed.
TEST_F(AccountsManagerUpdateTest, UnregisterClearsLastRegError)
{
    auto mgr = makeManager();
    compactphone::sip::Account a;
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr->add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    // Find the backend id from the command log.
    compactphone::sipbackend::AccountId backendId =
        compactphone::sipbackend::kInvalidAccountId;
    for (const auto &entry : fake.commandLog()) {
        if (entry.rfind("addAccount:", 0) == 0) {
            const auto first = entry.find(':', 11);
            if (first != std::string::npos)
                backendId = std::stoi(entry.substr(11, first - 11));
            break;
        }
    }
    ASSERT_NE(backendId, compactphone::sipbackend::kInvalidAccountId)
        << "No addAccount entry in command log";

    // Drive a registration failure through the listener path.
    fake.simulateRegState(backendId, /*regActive=*/false, /*sipCode=*/403,
                          "Forbidden");
    pumpEvents();

    EXPECT_EQ(mgr->stateOf(id), compactphone::sip::RegistrationState::Failed);
    EXPECT_EQ(mgr->lastRegErrorOf(id).code, 403);

    // Disabling the account should unregister it and clear the error.
    ASSERT_TRUE(mgr->setEnabled(id, false));

    EXPECT_EQ(mgr->stateOf(id), compactphone::sip::RegistrationState::Unregistered);
    EXPECT_TRUE(mgr->lastRegErrorOf(id).empty())
        << "lastRegErrorOf must be empty after unregister";

    fake.setListener(nullptr);
}

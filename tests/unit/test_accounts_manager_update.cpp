#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

class AccountsManagerUpdateTest : public ::testing::Test {
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

TEST_F(AccountsManagerUpdateTest, UpdatePersistsChangedFields)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Original"; a.username = "1001";
    a.domain = "asterisk:5060"; a.enabled = false;
    const auto id = mgr.add(a, "secret");

    auto edited = mgr.find(id).value();
    edited.displayName = "Renamed";
    edited.dtmfMethod = compactphone::sip::DtmfMethod::Info;
    edited.proxy = "proxy.example.com:5060";
    ASSERT_TRUE(mgr.update(edited));

    auto fresh = mgr.find(id).value();
    EXPECT_EQ(fresh.displayName, "Renamed");
    EXPECT_EQ(fresh.dtmfMethod, compactphone::sip::DtmfMethod::Info);
    EXPECT_EQ(fresh.proxy, "proxy.example.com:5060");
}

TEST_F(AccountsManagerUpdateTest, AddPersistsFullAccountAndReloadsFromDatabase)
{
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
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

        const auto id = mgr.add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        ASSERT_TRUE(kc.get(mgr.passwordRefFor(id)).has_value());
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto accounts = reloaded.list();
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
}

TEST_F(AccountsManagerUpdateTest, UpdateUnknownIdReturnsFalse)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    compactphone::sip::Account ghost;
    ghost.id = 9999;
    EXPECT_FALSE(mgr.update(ghost));
    EXPECT_FALSE(mgr.remove(ghost.id));
    EXPECT_FALSE(mgr.setDefault(ghost.id));
    EXPECT_EQ(mgr.stateOf(ghost.id), compactphone::sip::RegistrationState::Unregistered);
}

TEST_F(AccountsManagerUpdateTest, SetDefaultFlipsFlagAndClearsOthers)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    compactphone::sip::Account a, b;
    a.displayName = "A"; a.username = "u1"; a.domain = "d"; a.enabled = false;
    a.isDefault = true;
    b.displayName = "B"; b.username = "u2"; b.domain = "d"; b.enabled = false;
    const auto idA = mgr.add(a, "pa");
    const auto idB = mgr.add(b, "pb");

    EXPECT_EQ(mgr.defaultAccountId(), compactphone::sip::kInvalidAccountId);
    // (Both disabled, no default returned.)

    ASSERT_TRUE(mgr.setDefault(idB));
    auto av = mgr.find(idA).value();
    auto bv = mgr.find(idB).value();
    EXPECT_FALSE(av.isDefault);
    EXPECT_TRUE(bv.isDefault);
}

TEST_F(AccountsManagerUpdateTest, UpdateDisablesLiveAccount)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Live";
    a.username = "1001";
    a.authUser = "1001";
    a.domain = "127.0.0.1:9";
    a.enabled = true;
    a.registerOnStartup = false;

    const auto id = mgr.add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(mgr.registerAccount(id));
    ASSERT_NE(mgr.pjAccountFor(id), nullptr);

    auto edited = mgr.find(id).value();
    edited.enabled = false;
    ASSERT_TRUE(mgr.update(edited));

    EXPECT_EQ(mgr.pjAccountFor(id), nullptr);
    EXPECT_EQ(mgr.stateOf(id), compactphone::sip::RegistrationState::Unregistered);
}

TEST_F(AccountsManagerUpdateTest, RemoveDeletesDatabaseRowAndPassword)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Remove";
    a.username = "1001";
    a.domain = "pbx.example.com";
    a.enabled = false;
    const auto id = mgr.add(a, "secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    const auto ref = mgr.passwordRefFor(id);
    ASSERT_TRUE(kc.get(ref).has_value());

    ASSERT_TRUE(mgr.remove(id));

    EXPECT_FALSE(kc.get(ref).has_value());
    EXPECT_FALSE(mgr.find(id).has_value());

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    EXPECT_FALSE(reloaded.find(id).has_value());
}

// The full-account round-trip above asserts ~18 fields but omits three that
// are bound on write yet never proven to survive a reload: zrtpEnabled (media
// encryption — security relevant), codecs, and authRealm. Pin them so an
// insert/rowToAccount column drift on any of these is caught.
TEST_F(AccountsManagerUpdateTest, AddRoundTripsZrtpCodecsAndAuthRealm)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        a.zrtpEnabled = true;
        a.codecs = "opus,alaw";
        a.authRealm = "realm.example";
        id = mgr.add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto loaded = reloaded.find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_TRUE(loaded->zrtpEnabled);
    EXPECT_EQ(loaded->codecs, "opus,alaw");
    EXPECT_EQ(loaded->authRealm, "realm.example");
}

// UpdatePersistsChangedFields proves only displayName/dtmfMethod/proxy survive
// update(). update() has its own column-bind code distinct from add(); pin the
// security/transport fields against the update path so a drift between the two
// bind sites is caught.
TEST_F(AccountsManagerUpdateTest, UpdateRoundTripsSecurityAndTransportFields)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        // Start at non-target values so the update must actually change them.
        a.transport = compactphone::sip::Transport::Udp;
        a.srtpMode = compactphone::sip::SrtpMode::Disabled;
        a.allowUntrustedCert = false;
        a.zrtpEnabled = false;
        a.codecs = "ulaw";
        a.authRealm = "old.realm";
        id = mgr.add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        auto edited = mgr.find(id).value();
        edited.transport = compactphone::sip::Transport::Tls;
        edited.srtpMode = compactphone::sip::SrtpMode::Required;
        edited.allowUntrustedCert = true;
        edited.zrtpEnabled = true;
        edited.codecs = "opus,alaw";
        edited.authRealm = "new.realm";
        ASSERT_TRUE(mgr.update(edited));
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto loaded = reloaded.find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_EQ(loaded->transport, compactphone::sip::Transport::Tls);
    EXPECT_EQ(loaded->srtpMode, compactphone::sip::SrtpMode::Required);
    EXPECT_TRUE(loaded->allowUntrustedCert);
    EXPECT_TRUE(loaded->zrtpEnabled);
    EXPECT_EQ(loaded->codecs, "opus,alaw");
    EXPECT_EQ(loaded->authRealm, "new.realm");
}

// setEnabled has its own UPDATE ... SET enabled bind site, distinct from add()
// and update(). Prove the flag is *persisted* (survives a cold reload), not
// just flipped in the in-memory entry. We assert only the persisted column and
// the bool return — never live registration state, which routes into pjsua2.
TEST_F(AccountsManagerUpdateTest, SetEnabledPersistsFlagAcrossReload)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "127.0.0.1:9"; // unroutable, no real registration attempt
        a.enabled = false;
        a.registerOnStartup = false;
        id = mgr.add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        // false -> true: persists and (headlessly) creates a pj account.
        ASSERT_TRUE(mgr.setEnabled(id, true));
    }

    {
        compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
        const auto loaded = reloaded.find(id);
        ASSERT_TRUE(loaded.has_value());
        EXPECT_TRUE(loaded->enabled);

        // true -> false: persists the disable via the unregister path.
        ASSERT_TRUE(reloaded.setEnabled(id, false));
    }

    compactphone::sip::AccountsManager reloaded2(&engine, &db, &kc);
    const auto loaded2 = reloaded2.find(id);
    ASSERT_TRUE(loaded2.has_value());
    EXPECT_FALSE(loaded2->enabled);
}

// Two contract branches of setEnabled that the persistence test doesn't reach:
// (a) requesting the value the account already has returns true and leaves it
// disabled (the no-op fast path), and (b) an unknown id returns false instead
// of crashing past the find_if. UpdateUnknownIdReturnsFalse covers
// update/remove/setDefault but not setEnabled. (The no-op assertion pins the
// observable result, not the in-memory fast path specifically — a redundant
// UPDATE would leave the same persisted value; the reload just confirms the
// account is still durably disabled afterwards.)
TEST_F(AccountsManagerUpdateTest, SetEnabledNoOpAndUnknownId)
{
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        compactphone::sip::Account a;
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        id = mgr.add(a, "secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

        // Already disabled: requesting disabled again returns true, stays off.
        EXPECT_TRUE(mgr.setEnabled(id, false));
        EXPECT_FALSE(mgr.find(id).value().enabled);

        // Unknown id: false, no crash past find_if.
        EXPECT_FALSE(mgr.setEnabled(9999, true));
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto loaded = reloaded.find(id);
    ASSERT_TRUE(loaded.has_value());
    EXPECT_FALSE(loaded->enabled);
}

// SetDefaultFlipsFlagAndClearsOthers asserts the in-memory entries, but not the
// persisted single-default invariant: setDefault runs UPDATE ... SET is_default
// = 0 WHERE is_default = 1 then sets the new one, inside a transaction. Reload
// from the same DB and prove exactly one account is default and it is the one
// we chose — catches a drift where the clear-others UPDATE stops persisting.
TEST_F(AccountsManagerUpdateTest, SetDefaultPersistsSingleDefaultInvariant)
{
    compactphone::sip::AccountId idA = compactphone::sip::kInvalidAccountId;
    compactphone::sip::AccountId idB = compactphone::sip::kInvalidAccountId;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        compactphone::sip::Account a, b;
        a.username = "u1"; a.domain = "d"; a.enabled = false; a.isDefault = true;
        b.username = "u2"; b.domain = "d"; b.enabled = false;
        idA = mgr.add(a, "pa");
        idB = mgr.add(b, "pb");
        ASSERT_NE(idA, compactphone::sip::kInvalidAccountId);
        ASSERT_NE(idB, compactphone::sip::kInvalidAccountId);

        ASSERT_TRUE(mgr.setDefault(idB));
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto accounts = reloaded.list();
    ASSERT_EQ(accounts.size(), 2u);
    int defaults = 0;
    compactphone::sip::AccountId defaultId = compactphone::sip::kInvalidAccountId;
    for (const auto &acc : accounts) {
        if (acc.isDefault) { ++defaults; defaultId = acc.id; }
    }
    EXPECT_EQ(defaults, 1);
    EXPECT_EQ(defaultId, idB);
}

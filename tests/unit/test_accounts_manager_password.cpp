// Coverage for AccountsManager's credential seam: setPassword / remove and
// the keychain reference lifecycle. A bug here writes the wrong password to
// the keychain, leaves a stale secret behind after an account is deleted, or
// crashes on an unknown id — all credential-correctness failures with a large
// blast radius. AccountsManagerUpdateTest covers field updates / add / remove
// of *fields*; these tests pin the *password* behaviour it does not exercise.
//
// Everything here keeps accounts enabled=false so registration never reaches
// pjsua2 (no live registrar in the unit container). The behaviour under test
// is keychain-observable, which is exactly the part that must stay correct.

#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

class AccountsManagerPasswordTest : public ::testing::Test {
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

    // A disabled account so add() never auto-registers into pjsua2.
    compactphone::sip::Account makeAccount() const
    {
        compactphone::sip::Account a;
        a.label = "Office";
        a.username = "1001";
        a.domain = "pbx.example.com";
        a.enabled = false;
        a.registerOnStartup = false;
        return a;
    }
};

TEST_F(AccountsManagerPasswordTest, AddStoresPasswordUnderAccountRef)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "first-secret");
}

TEST_F(AccountsManagerPasswordTest, SetPasswordReplacesKeychainValue)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "rotated-secret");
}

TEST_F(AccountsManagerPasswordTest, SetPasswordKeepsTheSameKeychainRef)
{
    // The ref is reused on purpose so in-flight pj::AuthCredInfo and the DB
    // row don't need rewriting; pin that invariant.
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto refBefore = mgr.passwordRefFor(id);
    ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));
    EXPECT_EQ(mgr.passwordRefFor(id), refBefore);
}

TEST_F(AccountsManagerPasswordTest, SetPasswordOnUnknownIdReturnsFalse)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    EXPECT_FALSE(mgr.setPassword(424242, "whatever"));
    // An unknown id must not resolve to any keychain ref either.
    EXPECT_TRUE(mgr.passwordRefFor(424242).empty());
}

TEST_F(AccountsManagerPasswordTest, AddStoresEmptyPasswordRatherThanRejectingIt)
{
    // An empty password is a legitimate (if unusual) state — it must be
    // stored as "" under the ref, distinct from "no entry".
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    const auto id = mgr.add(makeAccount(), "");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "");
}

TEST_F(AccountsManagerPasswordTest, RemoveErasesTheStoredPassword)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto ref = mgr.passwordRefFor(id);
    ASSERT_TRUE(kc.get(ref).has_value());

    ASSERT_TRUE(mgr.remove(id));

    // No stale credential must survive deletion.
    EXPECT_FALSE(kc.get(ref).has_value());
    EXPECT_FALSE(mgr.find(id).has_value());
}

TEST_F(AccountsManagerPasswordTest, RemoveOnUnknownIdReturnsFalse)
{
    compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
    EXPECT_FALSE(mgr.remove(424242));
}

TEST_F(AccountsManagerPasswordTest, SetPasswordPersistsForAReloadedManager)
{
    // A second manager built on the same DB + keychain must read the rotated
    // password under the same ref — i.e. the credential the next session
    // registers with is the updated one, not the original.
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    std::string originalRef;
    {
        compactphone::sip::AccountsManager mgr(&engine, &db, &kc);
        id = mgr.add(makeAccount(), "first-secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));
        originalRef = mgr.passwordRefFor(id);
        ASSERT_FALSE(originalRef.empty());
    }

    compactphone::sip::AccountsManager reloaded(&engine, &db, &kc);
    const auto accounts = reloaded.list();
    ASSERT_EQ(accounts.size(), 1u);
    const auto reloadedId = accounts.front().id;

    // The ref must round-trip through the DB row — otherwise a password_ref
    // column mapping bug would silently point the reloaded account at the
    // wrong (or no) secret.
    EXPECT_EQ(reloaded.passwordRefFor(reloadedId), originalRef);

    const auto stored = kc.get(reloaded.passwordRefFor(reloadedId));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "rotated-secret");
}

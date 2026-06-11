// Coverage for AccountsManager's credential seam: setPassword / remove and
// the keychain reference lifecycle. A bug here writes the wrong password to
// the keychain, leaves a stale secret behind after an account is deleted, or
// crashes on an unknown id — all credential-correctness failures with a large
// blast radius. AccountsManagerUpdateTest covers field updates / add / remove
// of *fields*; these tests pin the *password* behaviour it does not exercise.
//
// Phase-2 rewrite: AccountsManager now drives registration through ISipBackend.
// Tests use a started FakeSipBackend + nullptr pjsipBridge. Accounts are
// kept enabled=false so registration never reaches a real backend. The
// behaviour under test is keychain-observable, which is exactly the part
// that must stay correct.

#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/sipbackend/fake/FakeSipBackend.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <QCoreApplication>

namespace {

QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_accounts_manager_password";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

} // namespace

class AccountsManagerPasswordTest : public ::testing::Test {
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

    // A disabled account so add() never auto-registers.
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
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "first-secret");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, SetPasswordReplacesKeychainValue)
{
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "rotated-secret");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, SetPasswordKeepsTheSameKeychainRef)
{
    // The ref is reused on purpose so in-flight backend credentials and the DB
    // row don't need rewriting; pin that invariant.
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto refBefore = mgr.passwordRefFor(id);
    ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));
    EXPECT_EQ(mgr.passwordRefFor(id), refBefore);
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, SetPasswordOnUnknownIdReturnsFalse)
{
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    EXPECT_FALSE(mgr.setPassword(424242, "whatever"));
    EXPECT_TRUE(mgr.passwordRefFor(424242).empty());
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, AddStoresEmptyPasswordRatherThanRejectingIt)
{
    // An empty password is a legitimate (if unusual) state — it must be
    // stored as "" under the ref, distinct from "no entry".
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    const auto id = mgr.add(makeAccount(), "");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto stored = kc.get(mgr.passwordRefFor(id));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "");
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, RemoveErasesTheStoredPassword)
{
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    const auto id = mgr.add(makeAccount(), "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    const auto ref = mgr.passwordRefFor(id);
    ASSERT_TRUE(kc.get(ref).has_value());

    ASSERT_TRUE(mgr.remove(id));

    // No stale credential must survive deletion.
    EXPECT_FALSE(kc.get(ref).has_value());
    EXPECT_FALSE(mgr.find(id).has_value());
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, RemoveOnUnknownIdReturnsFalse)
{
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    EXPECT_FALSE(mgr.remove(424242));
    fake.setListener(nullptr);
}

TEST_F(AccountsManagerPasswordTest, SetPasswordPersistsForAReloadedManager)
{
    // A second manager built on the same DB + keychain must read the rotated
    // password under the same ref — i.e. the credential the next session
    // registers with is the updated one, not the original.
    compactphone::sip::AccountId id = compactphone::sip::kInvalidAccountId;
    std::string originalRef;
    {
        compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
        fake.setListener(&mgr);
        id = mgr.add(makeAccount(), "first-secret");
        ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
        ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));
        originalRef = mgr.passwordRefFor(id);
        ASSERT_FALSE(originalRef.empty());
        fake.setListener(nullptr);
    }

    compactphone::sip::AccountsManager reloaded(&fake, nullptr, &db, &kc);
    fake.setListener(&reloaded);
    const auto accounts = reloaded.list();
    ASSERT_EQ(accounts.size(), 1u);
    const auto reloadedId = accounts.front().id;

    EXPECT_EQ(reloaded.passwordRefFor(reloadedId), originalRef);

    const auto stored = kc.get(reloaded.passwordRefFor(reloadedId));
    ASSERT_TRUE(stored.has_value());
    EXPECT_EQ(*stored, "rotated-secret");
    fake.setListener(nullptr);
}

// setPassword on a live account: verify that the backend receives a
// removeAccount + addAccount pair (re-registration with new credentials).
TEST_F(AccountsManagerPasswordTest, SetPasswordOnEnabledAccountReregisters)
{
    compactphone::sip::AccountsManager mgr(&fake, nullptr, &db, &kc);
    fake.setListener(&mgr);

    compactphone::sip::Account a = makeAccount();
    a.enabled = true;
    a.registerOnStartup = true;
    const auto id = mgr.add(a, "first-secret");
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);

    // Should have one addAccount in the log.
    {
        const auto &log = fake.commandLog();
        const int adds = static_cast<int>(std::count_if(log.begin(), log.end(),
            [](const std::string &s){ return s.rfind("addAccount:", 0) == 0; }));
        EXPECT_EQ(adds, 1);
    }

    ASSERT_TRUE(mgr.setPassword(id, "rotated-secret"));

    // setPassword unregisters (removeAccount) then re-registers (addAccount).
    {
        const auto &log = fake.commandLog();
        const int adds = static_cast<int>(std::count_if(log.begin(), log.end(),
            [](const std::string &s){ return s.rfind("addAccount:", 0) == 0; }));
        const int removes = static_cast<int>(std::count_if(log.begin(), log.end(),
            [](const std::string &s){ return s.rfind("removeAccount:", 0) == 0; }));
        EXPECT_EQ(adds, 2)    << "Expected two addAccount calls (initial + after setPassword)";
        EXPECT_EQ(removes, 1) << "Expected one removeAccount call (before setPassword re-reg)";
    }

    fake.setListener(nullptr);
}

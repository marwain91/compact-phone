#include <gtest/gtest.h>

#include "core/Contact.h"
#include "core/ContactsManager.h"
#include "persistence/Database.h"

class ContactsManagerTest : public ::testing::Test {
protected:
    compactphone::persistence::Database db;
    void SetUp() override { ASSERT_TRUE(db.openInMemory()); }
};

TEST_F(ContactsManagerTest, AddListUpdateRemove)
{
    compactphone::sip::ContactsManager mgr(&db);
    compactphone::sip::Contact c;
    c.displayName = "Alice";
    c.sipUri = "sip:alice@example.com";
    c.phone = "+1-555-0100";
    const auto id = mgr.add(c);
    ASSERT_NE(id, compactphone::sip::kInvalidContactId);

    EXPECT_EQ(mgr.list().size(), 1u);

    auto fetched = mgr.findById(id).value();
    EXPECT_EQ(fetched.displayName, "Alice");

    fetched.displayName = "Alice B.";
    EXPECT_TRUE(mgr.update(fetched));
    EXPECT_EQ(mgr.findById(id).value().displayName, "Alice B.");

    EXPECT_TRUE(mgr.remove(id));
    EXPECT_EQ(mgr.list().size(), 0u);
}

TEST_F(ContactsManagerTest, FindByUri)
{
    compactphone::sip::ContactsManager mgr(&db);
    // Plain field assignments instead of C++20 designated initializers
    // — MSVC's /std:c++17 mode rejects them while gcc/clang accept them
    // as an extension. We're not on C++20 project-wide yet.
    compactphone::sip::Contact c;
    c.displayName = "Bob";
    c.sipUri = "sip:bob@example.com";
    mgr.add(c);
    auto found = mgr.findByUri("sip:bob@example.com");
    ASSERT_TRUE(found.has_value());
    EXPECT_EQ(found->displayName, "Bob");
    EXPECT_FALSE(mgr.findByUri("sip:nobody@example.com").has_value());
}

TEST_F(ContactsManagerTest, UpdateAndRemoveUnknownContactReturnFalse)
{
    compactphone::sip::ContactsManager mgr(&db);
    compactphone::sip::Contact missing;
    missing.id = 12345;
    missing.displayName = "Missing";
    missing.sipUri = "sip:missing@example.com";

    EXPECT_FALSE(mgr.update(missing));
    EXPECT_FALSE(mgr.remove(missing.id));
    EXPECT_FALSE(mgr.findById(missing.id).has_value());
}

TEST_F(ContactsManagerTest, ListOrdersByDisplayName)
{
    compactphone::sip::ContactsManager mgr(&db);
    compactphone::sip::Contact zed;
    zed.displayName = "Zed";
    zed.sipUri = "sip:zed@example.com";
    compactphone::sip::Contact amy;
    amy.displayName = "Amy";
    amy.sipUri = "sip:amy@example.com";
    ASSERT_NE(mgr.add(zed), compactphone::sip::kInvalidContactId);
    ASSERT_NE(mgr.add(amy), compactphone::sip::kInvalidContactId);

    const auto contacts = mgr.list();
    ASSERT_EQ(contacts.size(), 2u);
    EXPECT_EQ(contacts[0].displayName, "Amy");
    EXPECT_EQ(contacts[1].displayName, "Zed");
}

#include <gtest/gtest.h>

#include "core/platform/Keychain_file.h"
#include "core/platform/Keychain_memory.h"

#include <QFile>
#include <QTemporaryDir>

TEST(KeychainMemory, RoundTripStoresAndRetrievesPassword)
{
    compactphone::platform::MemoryKeychain kc;
    ASSERT_TRUE(kc.set("ref-1", "hunter2"));
    auto pw = kc.get("ref-1");
    ASSERT_TRUE(pw.has_value());
    EXPECT_EQ(*pw, "hunter2");
}

TEST(KeychainMemory, GetMissingReturnsNullopt)
{
    compactphone::platform::MemoryKeychain kc;
    EXPECT_FALSE(kc.get("missing").has_value());
}

TEST(KeychainMemory, EraseRemovesEntry)
{
    compactphone::platform::MemoryKeychain kc;
    kc.set("ref-1", "secret");
    EXPECT_TRUE(kc.erase("ref-1"));
    EXPECT_FALSE(kc.get("ref-1").has_value());
}

TEST(KeychainMemory, EraseMissingReturnsFalse)
{
    compactphone::platform::MemoryKeychain kc;
    EXPECT_FALSE(kc.erase("missing"));
}

TEST(KeychainFile, RoundTripsAcrossInstances)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();

    {
        compactphone::platform::FileKeychain kc(path);
        ASSERT_TRUE(kc.open());
        ASSERT_TRUE(kc.set("ref-1", "hunter2"));
        ASSERT_TRUE(kc.set("ref-2", "p@ssw0rd!"));
    }

    {
        compactphone::platform::FileKeychain kc(path);
        ASSERT_TRUE(kc.open());
        auto p1 = kc.get("ref-1");
        auto p2 = kc.get("ref-2");
        ASSERT_TRUE(p1.has_value());
        ASSERT_TRUE(p2.has_value());
        EXPECT_EQ(*p1, "hunter2");
        EXPECT_EQ(*p2, "p@ssw0rd!");
    }
}

TEST(KeychainFile, RejectsTamperedCiphertext)
{
    QTemporaryDir tmp;
    const std::string path = tmp.filePath("creds.enc").toStdString();

    {
        compactphone::platform::FileKeychain kc(path);
        ASSERT_TRUE(kc.open());
        ASSERT_TRUE(kc.set("ref-1", "secret"));
    }

    // Flip one byte of the ciphertext.
    QFile f(QString::fromStdString(path));
    ASSERT_TRUE(f.open(QIODevice::ReadWrite));
    f.seek(f.size() - 1);
    const auto byte = f.read(1);
    f.seek(f.size() - 1);
    char flipped = byte[0] ^ 0x01;
    f.write(&flipped, 1);
    f.close();

    compactphone::platform::FileKeychain kc(path);
    EXPECT_FALSE(kc.open()); // GCM auth tag fails
}

TEST(KeychainFile, EraseRemovesEntry)
{
    QTemporaryDir tmp;
    const std::string path = tmp.filePath("creds.enc").toStdString();

    compactphone::platform::FileKeychain kc(path);
    ASSERT_TRUE(kc.open());
    kc.set("ref-1", "secret");
    EXPECT_TRUE(kc.erase("ref-1"));
    EXPECT_FALSE(kc.get("ref-1").has_value());
}

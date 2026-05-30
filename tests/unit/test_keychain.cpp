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

// A keychain file shorter than salt(16)+iv(12)+tag(16)=44 bytes cannot hold a
// valid GCM frame. open() must reject it (return false) rather than read past
// the buffer or initialise a silent empty store — a truncated on-disk keychain
// is a corruption signal, not "no passwords". This pins the observable contract
// ("too-short / corrupt file => open() returns false, no crash"), so a
// regression that made open() treat a truncated file as an empty store and
// return success (silently dropping every stored SIP password) is caught.
// Note: the false return for sub-44-byte input is enforced jointly by the early
// size guard and the inner decrypt() tag-length check, so this does not isolate
// one branch from the other — the contract is what matters here.
TEST(KeychainFile, TruncatedFileFailsToOpen)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString qpath = tmp.filePath("creds.enc");

    QFile f(qpath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    const QByteArray garbage("not-a-keychain", 14); // 14 bytes < 44
    ASSERT_EQ(f.write(garbage), garbage.size());
    f.close();

    compactphone::platform::FileKeychain kc(qpath.toStdString());
    EXPECT_FALSE(kc.open()); // too short to contain salt + iv + tag
}

// Boundary case: a file exactly at the salt size (16) but still below the
// 44-byte minimum. Proves open() returns false and does not throw when the blob
// carries a full salt but no iv/ciphertext (mid() past the end yields empty
// slices) — same observable contract as above at a different short length.
TEST(KeychainFile, SaltOnlyFileFailsToOpen)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString qpath = tmp.filePath("creds.enc");

    QFile f(qpath);
    ASSERT_TRUE(f.open(QIODevice::WriteOnly));
    const QByteArray saltOnly(16, '\0'); // exactly kSaltSize, no iv/tag
    ASSERT_EQ(f.write(saltOnly), saltOnly.size());
    f.close();

    compactphone::platform::FileKeychain kc(qpath.toStdString());
    EXPECT_FALSE(kc.open());
}

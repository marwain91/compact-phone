#include <gtest/gtest.h>

#include "core/platform/Keychain_file.h"
#include "core/platform/Keychain_memory.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#ifdef Q_OS_UNIX
#include <csignal>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#endif

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

#ifdef Q_OS_UNIX
// Failure injection for persist(): cap the maximum file size this process may
// write (RLIMIT_FSIZE) so the keychain's data writes fail mid-stream with
// EFBIG — the same shape as a disk filling up. This reproduces the historical
// data-loss bug: the old truncate-then-write persist() zeroed the only copy of
// the credential store at open(), then reported success even though the write
// was cut short, so the next open() found a truncated blob and every stored
// SIP password was gone. An atomic temp-file + rename persist must instead
// report failure and leave the previous file byte-for-byte intact.
//
// RLIMIT_FSIZE is used (rather than a chmod'd read-only directory) because it
// also fails writes for root — the Linux dev container runs tests as root,
// where permission-based injections are silently bypassed. SIGXFSZ must be
// ignored for the duration or the kernel kills the process instead of failing
// the write.
TEST(KeychainFile, FailedPersistReturnsFalseAndPreservesPreviousContents)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();

    compactphone::platform::FileKeychain kc(path);
    ASSERT_TRUE(kc.open());
    ASSERT_TRUE(kc.set("ref-1", "hunter2"));

    struct rlimit old{};
    ASSERT_EQ(getrlimit(RLIMIT_FSIZE, &old), 0);
    const auto prevHandler = std::signal(SIGXFSZ, SIG_IGN);
    const struct rlimit capped{20, old.rlim_max}; // < salt(16)+iv(12)+tag(16)
    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &capped), 0);

    // No ASSERTs inside the capped window — an early exit here would leave the
    // limit applied for every later test in this process.
    const bool setOk = kc.set("ref-2", "newpw");
    const bool eraseOk = kc.erase("ref-1");

    ASSERT_EQ(setrlimit(RLIMIT_FSIZE, &old), 0);
    std::signal(SIGXFSZ, prevHandler);

    EXPECT_FALSE(setOk);
    EXPECT_FALSE(eraseOk);
    // Failed mutations must not linger in the in-memory store either, or a
    // later unrelated set() would silently persist them.
    EXPECT_FALSE(kc.get("ref-2").has_value());
    EXPECT_TRUE(kc.get("ref-1").has_value());

    // The on-disk store must still hold exactly the pre-failure contents.
    compactphone::platform::FileKeychain kc2(path);
    ASSERT_TRUE(kc2.open());
    const auto p1 = kc2.get("ref-1");
    ASSERT_TRUE(p1.has_value());
    EXPECT_EQ(*p1, "hunter2");
    EXPECT_FALSE(kc2.get("ref-2").has_value());

    // The failed persists must not leave temp-file residue beside the store
    // (only creds.enc and creds.enc.key may exist).
    const QStringList entries = QDir(tmp.path()).entryList(
        QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
    EXPECT_EQ(entries.size(), 2) << entries.join(", ").toStdString();
}

// Both keychain files hold secrets (the master key outright; the blob is
// only as strong as an attacker's ability to read both), so they must be
// owner-only (0600) from the moment they exist. The writers set permissions
// explicitly on the QSaveFile before any secret byte is written; this pins
// the result, because QSaveFile::commit() on a brand-new target otherwise
// applies default umask-derived permissions (0666 & ~umask) — dropping or
// reordering the setPermissions call would silently regress to world-
// readable. The test runs under umask(0), the most permissive setting, so
// such a regression shows up as 0666 instead of hiding behind a developer's
// restrictive umask. stat() works the same for root, which is how the Linux
// dev container runs tests.
TEST(KeychainFile, StoreAndMasterKeyAreOwnerOnlyAfterCreation)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();

    const mode_t prevUmask = ::umask(0);
    // No ASSERTs inside the umask(0) window — an early exit here would leave
    // the permissive umask applied for every later test in this process.
    compactphone::platform::FileKeychain kc(path);
    const bool opened = kc.open();          // creates .key and the blob
    const bool stored = kc.set("ref-1", "hunter2"); // re-persists the blob
    ::umask(prevUmask);
    ASSERT_TRUE(opened);
    ASSERT_TRUE(stored);

    const auto modeOf = [](const std::string &p) -> mode_t {
        struct stat st{};
        if (::stat(p.c_str(), &st) != 0) return static_cast<mode_t>(~0u);
        return st.st_mode & static_cast<mode_t>(0777);
    };
    EXPECT_EQ(modeOf(path), static_cast<mode_t>(0600)) << "creds.enc";
    EXPECT_EQ(modeOf(path + ".key"), static_cast<mode_t>(0600))
        << "creds.enc.key";
}
#endif

namespace {

// Seeds a keychain at `path` with one credential and returns the resulting
// blob bytes, so master-key-loss tests can assert the blob survives a failed
// open() byte-for-byte.
QByteArray seedKeychainAndReadBlob(const std::string &path)
{
    compactphone::platform::FileKeychain kc(path);
    if (!kc.open() || !kc.set("ref-1", "hunter2")) return {};
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

QByteArray readFileBytes(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}

} // namespace

// The master key (.key sidecar) is the only way to decrypt the blob. If it
// goes missing — wiped by a cleanup tool, lost in a partial restore — open()
// mints a fresh key, the derived AES key no longer matches, and GCM auth must
// fail. The contract pinned here: open() returns false (fail loudly, so the
// UI can say "keychain unreadable" instead of showing empty accounts) and the
// blob is left byte-for-byte intact — never replaced with an empty store,
// which would destroy every saved SIP password the moment the user restores
// the .key from backup.
TEST(KeychainFile, MissingMasterKeyFailsToOpenAndPreservesBlob)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();
    const QByteArray blobBefore = seedKeychainAndReadBlob(path);
    ASSERT_FALSE(blobBefore.isEmpty());

    ASSERT_TRUE(QFile::remove(tmp.filePath("creds.enc.key")));

    compactphone::platform::FileKeychain kc(path);
    EXPECT_FALSE(kc.open());
    EXPECT_FALSE(kc.get("ref-1").has_value()); // nothing decrypted
    EXPECT_EQ(readFileBytes(tmp.filePath("creds.enc")), blobBefore);
}

// A .key sidecar with the wrong length (truncated copy, disk corruption) is
// rejected by the size guard before the blob is ever read. Same contract:
// open() false, blob untouched.
TEST(KeychainFile, WrongSizeMasterKeyFailsToOpenAndPreservesBlob)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();
    const QByteArray blobBefore = seedKeychainAndReadBlob(path);
    ASSERT_FALSE(blobBefore.isEmpty());

    QFile kf(tmp.filePath("creds.enc.key"));
    ASSERT_TRUE(kf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(kf.write(QByteArray(16, 'x')), 16); // half a key
    kf.close();

    compactphone::platform::FileKeychain kc(path);
    EXPECT_FALSE(kc.open());
    EXPECT_EQ(readFileBytes(tmp.filePath("creds.enc")), blobBefore);
}

// A .key of the correct 32-byte length but with different bytes (bit rot,
// restore from the wrong machine) passes the size guard and fails only at
// GCM authentication. This isolates the decrypt-failure branch of open()
// from the size guard the previous test exercises: it too must return false
// and leave the blob intact, not "recover" into an empty store.
TEST(KeychainFile, CorruptMasterKeyFailsToOpenAndPreservesBlob)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string path = tmp.filePath("creds.enc").toStdString();
    const QByteArray blobBefore = seedKeychainAndReadBlob(path);
    ASSERT_FALSE(blobBefore.isEmpty());

    const QString keyPath = tmp.filePath("creds.enc.key");
    QByteArray key = readFileBytes(keyPath);
    ASSERT_EQ(key.size(), 32);
    key[0] = key[0] ^ 0x01; // a single flipped bit is a different key
    QFile kf(keyPath);
    ASSERT_TRUE(kf.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(kf.write(key), key.size());
    kf.close();

    compactphone::platform::FileKeychain kc(path);
    EXPECT_FALSE(kc.open());
    EXPECT_FALSE(kc.get("ref-1").has_value());
    EXPECT_EQ(readFileBytes(tmp.filePath("creds.enc")), blobBefore);
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

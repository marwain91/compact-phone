#include "core/platform/Keychain_factory.h"

#include <gtest/gtest.h>

#include <QDir>
#include <QTemporaryDir>

namespace platform = compactphone::platform;

TEST(KeychainFactoryTest, ProducesAWorkingBackend)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const std::string fallback =
        QDir(tmp.path()).filePath("creds.enc").toStdString();

    auto kc = platform::makeKeychain(fallback, /*forceFile=*/true);
    ASSERT_NE(kc.get(), nullptr);

    EXPECT_TRUE(kc->set("test-ref", "hunter2"));
    const auto got = kc->get("test-ref");
    ASSERT_TRUE(got.has_value());
    EXPECT_EQ(*got, "hunter2");
    EXPECT_TRUE(kc->erase("test-ref"));
    EXPECT_FALSE(kc->get("test-ref").has_value());
}

TEST(KeychainFactoryTest, ForceFileBypassesNativeBackend)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    const QString path = QDir(tmp.path()).filePath("forced.enc");

    auto kc = platform::makeKeychain(path.toStdString(), /*forceFile=*/true);
    ASSERT_NE(kc.get(), nullptr);
    ASSERT_TRUE(kc->set("ref", "secret"));
    EXPECT_EQ(*kc->get("ref"), "secret");

    // forceFile must have produced an on-disk file (proving we didn't
    // route to native).
    EXPECT_TRUE(QFile::exists(path))
        << "forceFile=true should write the fallback file at " << path.toStdString();
}

TEST(KeychainFactoryTest, GetReturnsNulloptForMissingRef)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    auto kc = platform::makeKeychain(
        QDir(tmp.path()).filePath("creds.enc").toStdString(),
        /*forceFile=*/true);
    EXPECT_FALSE(kc->get("never-stored").has_value());
}

TEST(KeychainFactoryTest, EraseOfMissingRefIsSafe)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    auto kc = platform::makeKeychain(
        QDir(tmp.path()).filePath("creds.enc").toStdString(),
        /*forceFile=*/true);
    // Should not throw / crash even if the ref never existed.
    kc->erase("nonexistent-ref");
}

#include "core/BootConfig.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QTemporaryFile>

namespace {

using compactphone::bootconfig::parseCommandLine;
using compactphone::bootconfig::resolvePassword;

// QCommandLineParser::process needs argv[0] to be the program name.
QStringList args(std::initializer_list<const char *> a)
{
    QStringList out;
    out << QStringLiteral("compactphone");
    for (const char *s : a) out << QString::fromUtf8(s);
    return out;
}

} // namespace

TEST(BootConfigTest, NoArgsProducesEmptyConfig)
{
    const auto cfg = parseCommandLine(args({}));
    EXPECT_TRUE(cfg.empty());
    EXPECT_TRUE(cfg.accounts.isEmpty());
}

TEST(BootConfigTest, AccountFlagsBuildOneAccountWithDefaults)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-user", "1001",
        "--sip-password", "secret",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    const auto &a = cfg.accounts.first();
    EXPECT_EQ(a.params.value("username").toString(), "1001");
    EXPECT_EQ(a.params.value("domain").toString(), "pbx.example.com");
    EXPECT_EQ(a.params.value("authUser").toString(), "1001");
    EXPECT_EQ(a.password, QStringLiteral("secret"));
    EXPECT_TRUE(a.registerOnStart);
    EXPECT_EQ(a.params.value("label").toString(),
              QStringLiteral("1001@pbx.example.com"));
}

TEST(BootConfigTest, MissingUserDropsCliAccount)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-password", "secret",
    }));
    EXPECT_TRUE(cfg.accounts.isEmpty());
}

TEST(BootConfigTest, TransportAndSrtpAreLowercased)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-user", "1001",
        "--sip-transport", "TLS",
        "--sip-srtp", "Mandatory",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("transport").toString(), "tls");
    EXPECT_EQ(cfg.accounts.first().params.value("srtpMode").toString(),
              "mandatory");
}

TEST(BootConfigTest, GlobalSwitchesArePropagated)
{
    const auto cfg = parseCommandLine(args({
        "--autoanswer",
        "--dnd",
        "--minimize-to-tray",
        "--theme", "midnight",
        "--log-level", "debug",
    }));
    ASSERT_TRUE(cfg.autoAnswer.has_value());
    EXPECT_TRUE(*cfg.autoAnswer);
    ASSERT_TRUE(cfg.dnd.has_value());
    EXPECT_TRUE(*cfg.dnd);
    ASSERT_TRUE(cfg.minimizeToTray.has_value());
    EXPECT_TRUE(*cfg.minimizeToTray);
    ASSERT_TRUE(cfg.theme.has_value());
    EXPECT_EQ(*cfg.theme, "midnight");
    ASSERT_TRUE(cfg.logLevel.has_value());
    EXPECT_EQ(*cfg.logLevel, "debug");
}

TEST(BootConfigTest, ReplaceAccountFlagIsCaptured)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-user", "1001",
        "--replace-account",
    }));
    EXPECT_TRUE(cfg.replaceAccounts);
}

TEST(BootConfigTest, ResolvePasswordFromFile)
{
    QTemporaryFile f;
    ASSERT_TRUE(f.open());
    f.write("hunter2\nextra-garbage\n");
    f.flush();
    const auto pw = resolvePassword(QStringLiteral("@file:") + f.fileName());
    EXPECT_EQ(pw, "hunter2");
}

TEST(BootConfigTest, ResolvePasswordFromEnv)
{
    qputenv("COMPACTPHONE_TEST_PWD", "from-env");
    const auto pw = resolvePassword(QStringLiteral("@env:COMPACTPHONE_TEST_PWD"));
    EXPECT_EQ(pw, "from-env");
    qunsetenv("COMPACTPHONE_TEST_PWD");
}

TEST(BootConfigTest, ResolvePasswordLiteralPassthrough)
{
    EXPECT_EQ(resolvePassword("plain-text"), "plain-text");
    EXPECT_TRUE(resolvePassword("").isEmpty());
}

TEST(BootConfigTest, LoadFromFileBuildsAccountsAndDefaults)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "replace_accounts": true,
        "accounts": [
            {
                "label": "ACME",
                "server": "pbx.acme.com",
                "user": "1001",
                "password": "literal-secret",
                "transport": "tls",
                "srtp": "mandatory",
                "codecs": ["opus", "PCMA"],
                "register_on_start": false
            }
        ],
        "defaults": {
            "theme": "midnight",
            "minimize_to_tray": true,
            "log_level": "debug"
        }
    })");
    f.flush();

    const auto cfg = compactphone::bootconfig::loadFromFile(f.fileName());
    ASSERT_EQ(cfg.accounts.size(), 1);
    const auto &a = cfg.accounts.first();
    EXPECT_EQ(a.params.value("domain").toString(), "pbx.acme.com");
    EXPECT_EQ(a.params.value("username").toString(), "1001");
    EXPECT_EQ(a.params.value("transport").toString(), "tls");
    EXPECT_EQ(a.params.value("srtpMode").toString(), "mandatory");
    EXPECT_EQ(a.params.value("codecs").toString(), "opus,PCMA");
    EXPECT_EQ(a.password, "literal-secret");
    EXPECT_FALSE(a.registerOnStart);

    EXPECT_TRUE(cfg.replaceAccounts);
    ASSERT_TRUE(cfg.theme.has_value());
    EXPECT_EQ(*cfg.theme, "midnight");
    ASSERT_TRUE(cfg.logLevel.has_value());
    EXPECT_EQ(*cfg.logLevel, "debug");
    ASSERT_TRUE(cfg.minimizeToTray.has_value());
    EXPECT_TRUE(*cfg.minimizeToTray);
}

TEST(BootConfigTest, LoadFromFileRejectsUnknownSchemaVersion)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({"schema_version": 99, "accounts": []})");
    f.flush();
    const auto cfg = compactphone::bootconfig::loadFromFile(f.fileName());
    EXPECT_TRUE(cfg.accounts.isEmpty());
    EXPECT_FALSE(cfg.theme.has_value());
}

TEST(BootConfigTest, LoadFromFileSkipsAccountsMissingMandatoryFields)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "accounts": [
            { "user": "1001", "server": "pbx" },
            { "user": "no-server-here" },
            { "server": "no-user-here.example" }
        ]
    })");
    f.flush();
    const auto cfg = compactphone::bootconfig::loadFromFile(f.fileName());
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("username").toString(), "1001");
}

TEST(BootConfigTest, RegisterOnStartCanBeDisabled)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-user", "1001",
        "--register-on-start", "no",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_FALSE(cfg.accounts.first().registerOnStart);
    EXPECT_FALSE(cfg.accounts.first().params
                     .value("registerOnStartup").toBool());
}

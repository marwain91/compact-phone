#include "core/BootConfig.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
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

QStringList args(const QStringList &a)
{
    QStringList out;
    out << QStringLiteral("compactphone");
    out << a;
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

TEST(BootConfigTest, AccountCliFlagsCoverAdvancedProvisioningFields)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com",
        "--sip-port", "5080",
        "--sip-user", "1001",
        "--sip-realm", "office",
        "--sip-proxy", "sbc.example.com:5060",
        "--sip-public-address", "203.0.113.10",
        "--sip-register-interval", "180",
        "--sip-keepalive-interval", "20",
        "--sip-session-timers", "no",
        "--sip-publish-presence",
        "--sip-ice",
        "--sip-hide-caller-id",
        "--sip-zrtp",
        "--sip-allow-untrusted-cert",
        "--sip-dtmf-method", "info",
        "--sip-enabled", "no",
        "--sip-default",
        "--sip-sort-order", "5",
    }));

    ASSERT_EQ(cfg.accounts.size(), 1);
    const auto &a = cfg.accounts.first();
    EXPECT_EQ(a.params.value("domain").toString(), "pbx.example.com:5080");
    EXPECT_EQ(a.params.value("authRealm").toString(), "office");
    EXPECT_EQ(a.params.value("proxy").toString(), "sbc.example.com:5060");
    EXPECT_EQ(a.params.value("publicAddress").toString(), "203.0.113.10");
    EXPECT_EQ(a.params.value("registerIntervalSec").toInt(), 180);
    EXPECT_EQ(a.params.value("keepaliveIntervalSec").toInt(), 20);
    EXPECT_TRUE(a.params.contains("sessionTimersEnabled"));
    EXPECT_FALSE(a.params.value("sessionTimersEnabled").toBool());
    EXPECT_TRUE(a.params.value("publishPresenceEnabled").toBool());
    EXPECT_TRUE(a.params.value("iceEnabled").toBool());
    EXPECT_TRUE(a.params.value("hideCallerId").toBool());
    EXPECT_TRUE(a.params.value("zrtpEnabled").toBool());
    EXPECT_TRUE(a.params.value("allowUntrustedCert").toBool());
    EXPECT_EQ(a.params.value("dtmfMethod").toString(), "info");
    EXPECT_TRUE(a.params.contains("enabled"));
    EXPECT_FALSE(a.params.value("enabled").toBool());
    EXPECT_TRUE(a.params.value("isDefault").toBool());
    EXPECT_EQ(a.params.value("sortOrder").toInt(), 5);
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

TEST(BootConfigTest, LoadFromFileMapsEveryProvisionableAccountField)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "accounts": [
            {
                "label": "Full",
                "server": "pbx.full.example",
                "realm": "full-realm",
                "user": "4001",
                "auth_user": "auth4001",
                "password": "full-secret",
                "display_name": "Full User",
                "transport": "tls",
                "proxy": "sbc.full.example:5061",
                "stun": "stun.full.example:3478",
                "public_address": "203.0.113.40",
                "codecs": "opus,PCMA",
                "voicemail": "*98",
                "register_on_start": false,
                "register_interval_sec": 120,
                "keepalive_interval_sec": 25,
                "session_timers_enabled": false,
                "publish_presence_enabled": true,
                "ice_enabled": true,
                "hide_caller_id": true,
                "zrtp_enabled": true,
                "srtp": "mandatory",
                "allow_untrusted_cert": true,
                "dtmf_method": "info",
                "enabled": false,
                "default": true,
                "sort_order": 9
            }
        ]
    })");
    f.flush();

    const auto cfg = compactphone::bootconfig::loadFromFile(f.fileName());

    ASSERT_EQ(cfg.accounts.size(), 1);
    const auto &a = cfg.accounts.first();
    EXPECT_EQ(a.params.value("label").toString(), "Full");
    EXPECT_EQ(a.params.value("domain").toString(), "pbx.full.example");
    EXPECT_EQ(a.params.value("authRealm").toString(), "full-realm");
    EXPECT_EQ(a.params.value("username").toString(), "4001");
    EXPECT_EQ(a.params.value("authUser").toString(), "auth4001");
    EXPECT_EQ(a.password, "full-secret");
    EXPECT_EQ(a.params.value("displayName").toString(), "Full User");
    EXPECT_EQ(a.params.value("transport").toString(), "tls");
    EXPECT_EQ(a.params.value("proxy").toString(), "sbc.full.example:5061");
    EXPECT_EQ(a.params.value("stunServer").toString(), "stun.full.example:3478");
    EXPECT_EQ(a.params.value("publicAddress").toString(), "203.0.113.40");
    EXPECT_EQ(a.params.value("codecs").toString(), "opus,PCMA");
    EXPECT_EQ(a.params.value("voicemailNumber").toString(), "*98");
    EXPECT_FALSE(a.params.value("registerOnStartup").toBool());
    EXPECT_EQ(a.params.value("registerIntervalSec").toInt(), 120);
    EXPECT_EQ(a.params.value("keepaliveIntervalSec").toInt(), 25);
    EXPECT_TRUE(a.params.contains("sessionTimersEnabled"));
    EXPECT_FALSE(a.params.value("sessionTimersEnabled").toBool());
    EXPECT_TRUE(a.params.value("publishPresenceEnabled").toBool());
    EXPECT_TRUE(a.params.value("iceEnabled").toBool());
    EXPECT_TRUE(a.params.value("hideCallerId").toBool());
    EXPECT_TRUE(a.params.value("zrtpEnabled").toBool());
    EXPECT_EQ(a.params.value("srtpMode").toString(), "mandatory");
    EXPECT_TRUE(a.params.value("allowUntrustedCert").toBool());
    EXPECT_EQ(a.params.value("dtmfMethod").toString(), "info");
    EXPECT_TRUE(a.params.contains("enabled"));
    EXPECT_FALSE(a.params.value("enabled").toBool());
    EXPECT_TRUE(a.params.value("isDefault").toBool());
    EXPECT_EQ(a.params.value("sortOrder").toInt(), 9);
}

TEST(BootConfigTest, LoadFromFileReadsHeadlessScenario)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "headless": {
            "call": "sip:600@pbx.example.com",
            "auto_answer": true,
            "play_file": "/tmp/prompt.wav",
            "loop_play_file": true,
            "duration_sec": 30,
            "exit_after_call": true
        }
    })");
    f.flush();

    const auto cfg = compactphone::bootconfig::loadFromFile(f.fileName());

    ASSERT_TRUE(cfg.headlessCallUri.has_value());
    EXPECT_EQ(*cfg.headlessCallUri, "sip:600@pbx.example.com");
    ASSERT_TRUE(cfg.headlessAutoAnswer.has_value());
    EXPECT_TRUE(*cfg.headlessAutoAnswer);
    ASSERT_TRUE(cfg.headlessPlayFile.has_value());
    EXPECT_EQ(*cfg.headlessPlayFile, "/tmp/prompt.wav");
    ASSERT_TRUE(cfg.headlessLoopPlayFile.has_value());
    EXPECT_TRUE(*cfg.headlessLoopPlayFile);
    ASSERT_TRUE(cfg.headlessDurationSec.has_value());
    EXPECT_EQ(*cfg.headlessDurationSec, 30);
    ASSERT_TRUE(cfg.headlessExitAfterCall.has_value());
    EXPECT_TRUE(*cfg.headlessExitAfterCall);
}

TEST(BootConfigTest, HeadlessCliFlagsOverrideConfig)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "headless": {
            "call": "sip:old@example.com",
            "auto_answer": false,
            "play_file": "/tmp/old.wav",
            "loop_play_file": false,
            "duration_sec": 10
        }
    })");
    f.flush();

    const auto cfg = parseCommandLine(args({
        QStringLiteral("--config"),
        f.fileName(),
        QStringLiteral("--call"),
        QStringLiteral("sip:new@example.com"),
        QStringLiteral("--auto-answer"),
        QStringLiteral("--play-file"),
        QStringLiteral("/tmp/new.wav"),
        QStringLiteral("--loop-play-file"),
        QStringLiteral("--duration-sec"),
        QStringLiteral("45"),
        QStringLiteral("--exit-after-call"),
    }));

    ASSERT_TRUE(cfg.headlessCallUri.has_value());
    EXPECT_EQ(*cfg.headlessCallUri, "sip:new@example.com");
    ASSERT_TRUE(cfg.headlessAutoAnswer.has_value());
    EXPECT_TRUE(*cfg.headlessAutoAnswer);
    ASSERT_TRUE(cfg.headlessPlayFile.has_value());
    EXPECT_EQ(*cfg.headlessPlayFile, "/tmp/new.wav");
    ASSERT_TRUE(cfg.headlessLoopPlayFile.has_value());
    EXPECT_TRUE(*cfg.headlessLoopPlayFile);
    ASSERT_TRUE(cfg.headlessDurationSec.has_value());
    EXPECT_EQ(*cfg.headlessDurationSec, 45);
    ASSERT_TRUE(cfg.headlessExitAfterCall.has_value());
    EXPECT_TRUE(*cfg.headlessExitAfterCall);
}

TEST(BootConfigTest, ParseCommandLineLoadsExplicitConfigFile)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "accounts": [
            {
                "server": "pbx.config.example",
                "user": "2001",
                "auth_user": "auth2001",
                "password": "from-config",
                "display_name": "Config User"
            }
        ],
        "defaults": {
            "theme": "ivory",
            "dnd": true
        }
    })");
    f.flush();

    const auto cfg = parseCommandLine(args({
        QStringLiteral("--config"),
        f.fileName(),
    }));

    ASSERT_EQ(cfg.accounts.size(), 1);
    const auto &a = cfg.accounts.first();
    EXPECT_EQ(a.params.value("domain").toString(), "pbx.config.example");
    EXPECT_EQ(a.params.value("username").toString(), "2001");
    EXPECT_EQ(a.params.value("authUser").toString(), "auth2001");
    EXPECT_EQ(a.params.value("displayName").toString(), "Config User");
    EXPECT_EQ(a.password, "from-config");
    ASSERT_TRUE(cfg.theme.has_value());
    EXPECT_EQ(*cfg.theme, "ivory");
    ASSERT_TRUE(cfg.dnd.has_value());
    EXPECT_TRUE(*cfg.dnd);
}

TEST(BootConfigTest, CliFlagsOverrideExplicitConfigDefaultsAndAppendAccount)
{
    QTemporaryFile f;
    f.setFileTemplate(QDir::tempPath() + "/cp-provXXXXXX.json");
    ASSERT_TRUE(f.open());
    f.write(R"({
        "schema_version": 1,
        "accounts": [
            { "server": "pbx.config.example", "user": "2001" }
        ],
        "defaults": {
            "theme": "light"
        }
    })");
    f.flush();

    const auto cfg = parseCommandLine(args({
        QStringLiteral("--config"),
        f.fileName(),
        QStringLiteral("--theme"),
        QStringLiteral("midnight"),
        QStringLiteral("--sip-server"),
        QStringLiteral("pbx.cli.example"),
        QStringLiteral("--sip-user"),
        QStringLiteral("3001"),
    }));

    ASSERT_EQ(cfg.accounts.size(), 2);
    EXPECT_EQ(cfg.accounts.first().params.value("domain").toString(),
              "pbx.config.example");
    EXPECT_EQ(cfg.accounts.last().params.value("domain").toString(),
              "pbx.cli.example");
    ASSERT_TRUE(cfg.theme.has_value());
    EXPECT_EQ(*cfg.theme, "midnight");
}

TEST(BootConfigTest, UserProvisioningOverridesSystemProvisioning)
{
    const QString systemDir = QStringLiteral("/etc/compactphone");
    const QString systemPath = systemDir + QStringLiteral("/provisioning.json");
    QFile oldSystem(systemPath);
    QByteArray oldSystemBytes;
    const bool hadOldSystem = oldSystem.exists();
    if (hadOldSystem) {
        ASSERT_TRUE(oldSystem.open(QIODevice::ReadOnly));
        oldSystemBytes = oldSystem.readAll();
        oldSystem.close();
    }

    if (!QDir().mkpath(systemDir)) {
        GTEST_SKIP() << "cannot create /etc/compactphone in this environment";
    }

    QFile systemFile(systemPath);
    ASSERT_TRUE(systemFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    systemFile.write(R"({
        "schema_version": 1,
        "defaults": { "theme": "dark" }
    })");
    systemFile.close();

    QStandardPaths::setTestModeEnabled(true);
    const QString userDir =
        QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    ASSERT_FALSE(userDir.isEmpty());
    ASSERT_TRUE(QDir().mkpath(userDir));
    QFile userFile(QDir(userDir).filePath(QStringLiteral("provisioning.json")));
    ASSERT_TRUE(userFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
    userFile.write(R"({
        "schema_version": 1,
        "defaults": { "theme": "ivory" }
    })");
    userFile.close();

    const auto cfg = parseCommandLine(args({}));

    if (hadOldSystem) {
        ASSERT_TRUE(systemFile.open(QIODevice::WriteOnly | QIODevice::Truncate));
        systemFile.write(oldSystemBytes);
        systemFile.close();
    } else {
        QFile::remove(systemPath);
    }
    QFile::remove(userFile.fileName());
    QStandardPaths::setTestModeEnabled(false);

    ASSERT_TRUE(cfg.theme.has_value());
    EXPECT_EQ(*cfg.theme, "ivory");
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

// --- server / --sip-port assembly (the IPv6 bracket logic) ----------------
// serverWithOptionalPort() is file-internal; exercise it through the public
// --sip-server / --sip-port seam and observe the resulting "domain". A bug
// here produces an unconnectable registrar address.

TEST(BootConfigTest, BareIpv6ServerIsBracketedWithExplicitPort)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "2001:db8::1",
        "--sip-port", "5060",
        "--sip-user", "1001",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("domain").toString(),
              "[2001:db8::1]:5060");
}

TEST(BootConfigTest, AlreadyBracketedIpv6WithPortIsLeftUnchanged)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "[2001:db8::1]:5061",
        "--sip-port", "5070",
        "--sip-user", "1001",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("domain").toString(),
              "[2001:db8::1]:5061");
}

TEST(BootConfigTest, HostnameThatAlreadyCarriesAPortIsLeftUnchanged)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "pbx.example.com:5060",
        "--sip-port", "5070",
        "--sip-user", "1001",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("domain").toString(),
              "pbx.example.com:5060");
}

// Characterizes CURRENT behaviour, which looks like a bug: an IPv6 literal
// supplied already-bracketed but without a port is wrapped a second time,
// yielding a double-bracketed, unconnectable address. See SUSPECTED BUGS in
// the qa-coverage report. If serverWithOptionalPort() is fixed to detect the
// existing brackets, update this expectation to "[2001:db8::1]:5060".
TEST(BootConfigTest, AlreadyBracketedIpv6WithoutPortIsDoubleBracketed)
{
    const auto cfg = parseCommandLine(args({
        "--sip-server", "[2001:db8::1]",
        "--sip-port", "5060",
        "--sip-user", "1001",
    }));
    ASSERT_EQ(cfg.accounts.size(), 1);
    EXPECT_EQ(cfg.accounts.first().params.value("domain").toString(),
              "[[2001:db8::1]]:5060");
}

// --- resolvePassword failure branches -------------------------------------
// The happy @file / @env / literal paths are already covered; pin the
// failure paths so a missing secret degrades to empty (warn) rather than
// throwing or returning garbage, which would break headless provisioning.

TEST(BootConfigTest, ResolvePasswordFromMissingFileIsEmpty)
{
    const QString missing =
        QDir::tempPath() + QStringLiteral("/cp-does-not-exist-XYZ.secret");
    QFile::remove(missing); // ensure absent
    EXPECT_TRUE(resolvePassword(QStringLiteral("@file:") + missing).isEmpty());
}

TEST(BootConfigTest, ResolvePasswordFromUnsetEnvIsEmpty)
{
    qunsetenv("COMPACTPHONE_DEFINITELY_UNSET_PWD");
    EXPECT_TRUE(
        resolvePassword(QStringLiteral("@env:COMPACTPHONE_DEFINITELY_UNSET_PWD"))
            .isEmpty());
}

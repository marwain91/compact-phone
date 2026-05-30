#include "BootConfig.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QTextStream>
#include <QVariant>
#include <QtGlobal>

namespace compactphone::bootconfig {

namespace {

void warn(const QString &msg)
{
    QTextStream(stderr) << "compactphone: " << msg << "\n";
}

} // namespace

void configureParser(QCommandLineParser &parser)
{
    parser.setApplicationDescription(
        "Compact Phone — a compact SIP softphone.\n"
        "\n"
        "All --sip-* flags create or update a SIP account at startup. The same\n"
        "flags can also be supplied via a JSON provisioning file passed\n"
        "with --config. CLI overrides take precedence over the file.\n");

    parser.addOptions({
        // --- account creation ---
        {"sip-server",
         "SIP server hostname or IP. Becomes registrar and default realm.",
         "host"},
        {"sip-port",
         "SIP server port (default: 5060 for UDP/TCP, 5061 for TLS).",
         "port"},
        {"sip-user",
         "SIP username (the part before @ in the AOR).",
         "user"},
        {"sip-auth-user",
         "Auth username if different from --sip-user.",
         "user"},
        {"sip-password",
         "SIP password. Insecure — leaks via `ps`. Prefer --sip-password-file "
         "or --sip-password-stdin or @env:VAR.",
         "password"},
        {"sip-password-file",
         "Read SIP password from this file (first line, trimmed).",
         "path"},
        {"sip-password-stdin",
         "Read SIP password from stdin (one line). Useful for piping from "
         "secret managers."},
        {"sip-realm",
         "SIP realm (defaults to --sip-server).",
         "realm"},
        {"sip-proxy",
         "Outbound SIP proxy, e.g. sbc.example.com:5060.",
         "host[:port]"},
        {"sip-transport",
         "SIP transport: udp, tcp, or tls (default: udp).",
         "transport"},
        {"sip-srtp",
         "SRTP mode: disabled, optional, or mandatory (default: optional).",
         "mode"},
        {"sip-display-name",
         "Display name shown to the remote party.",
         "name"},
        {"sip-stun",
         "STUN server (host or host:port).",
         "host"},
        {"sip-public-address",
         "Public Contact address to advertise instead of STUN discovery.",
         "host[:port]"},
        {"sip-codecs",
         "Comma-separated codec priority list, e.g. opus,PCMA,PCMU.",
         "list"},
        {"sip-voicemail",
         "Voicemail access number or URI.",
         "number"},
        {"sip-register-interval",
         "REGISTER refresh interval in seconds.",
         "seconds"},
        {"sip-keepalive-interval",
         "UDP keepalive interval in seconds.",
         "seconds"},
        {"sip-session-timers",
         "Enable SIP session timers: yes or no (default: yes).",
         "yes-no"},
        {"sip-publish-presence",
         "Publish SIMPLE presence for this account."},
        {"no-sip-publish-presence",
         "Disable SIMPLE presence publishing for this account."},
        {"sip-ice",
         "Enable ICE media negotiation."},
        {"no-sip-ice",
         "Disable ICE media negotiation."},
        {"sip-hide-caller-id",
         "Use an anonymous From identity for outbound calls."},
        {"no-sip-hide-caller-id",
         "Use the configured account identity for outbound calls."},
        {"sip-zrtp",
         "Enable ZRTP flag in the account profile."},
        {"no-sip-zrtp",
         "Disable ZRTP flag in the account profile."},
        {"sip-allow-untrusted-cert",
         "Allow untrusted TLS certificates for this account."},
        {"no-sip-allow-untrusted-cert",
         "Require trusted TLS certificates for this account."},
        {"sip-dtmf-method",
         "DTMF method: inband, rfc2833, or info.",
         "method"},
        {"sip-enabled",
         "Enable this account: yes or no (default: yes).",
         "yes-no"},
        {"sip-default",
         "Make this account the default account."},
        {"sip-sort-order",
         "Account sort order.",
         "number"},
        {"sip-label",
         "Friendly label for the account (defaults to <user>@<server>).",
         "label"},
        {"register-on-start",
         "Register this account on startup: yes or no (default: yes).",
         "yes-no", "yes"},
        {"replace-account",
         "If an account with the same username and server already exists, "
         "update it instead of adding a duplicate."},

        // --- global startup behavior ---
        {"autoanswer",
         "Enable auto-answer for incoming calls."},
        {"auto-answer",
         "Headless alias for auto-answering incoming calls."},
        {"dnd",
         "Enable Do Not Disturb (reject all incoming)."},
        {"minimize-to-tray",
         "Start minimized to the system tray."},
        {"theme",
         "Initial theme: light, dark, midnight, ivory, or velvet.",
         "id"},
        {"log-level",
         "Log level: trace, debug, info, warn, error.",
         "level"},
        {"log-file",
         "Append logs to this file.",
         "path"},
        {"config",
         "Read additional provisioning settings from this JSON file. "
         "CLI flags override file values.",
         "path"},

        // --- headless SIP test runner ---
        {"call",
         "Headless mode: place an outbound call to this SIP URI.",
         "sip-uri"},
        {"play-file",
         "Headless mode: WAV file to transmit when media is confirmed.",
         "path"},
        {"loop-play-file",
         "Headless mode: loop --play-file until the call ends."},
        {"duration-sec",
         "Headless mode: quit after this many seconds.",
         "seconds"},
        {"exit-after-call",
         "Headless mode: quit when the active call disconnects."},
    });

    parser.addHelpOption();
    parser.addVersionOption();
}

namespace {

// Merge `incoming` into `base`. Accounts append; scalars in `incoming` win
// when set (so CLI overrides file overrides earlier files).
void mergeInto(BootConfig &base, const BootConfig &incoming)
{
    base.accounts.append(incoming.accounts);
    if (incoming.autoAnswer)     base.autoAnswer = *incoming.autoAnswer;
    if (incoming.dnd)            base.dnd = *incoming.dnd;
    if (incoming.minimizeToTray) base.minimizeToTray = *incoming.minimizeToTray;
    if (incoming.theme)          base.theme = *incoming.theme;
    if (incoming.logLevel)       base.logLevel = *incoming.logLevel;
    if (incoming.logFile)        base.logFile = *incoming.logFile;
    if (incoming.headlessCallUri) base.headlessCallUri = *incoming.headlessCallUri;
    if (incoming.headlessAutoAnswer)
        base.headlessAutoAnswer = *incoming.headlessAutoAnswer;
    if (incoming.headlessPlayFile)
        base.headlessPlayFile = *incoming.headlessPlayFile;
    if (incoming.headlessLoopPlayFile)
        base.headlessLoopPlayFile = *incoming.headlessLoopPlayFile;
    if (incoming.headlessDurationSec)
        base.headlessDurationSec = *incoming.headlessDurationSec;
    if (incoming.headlessExitAfterCall)
        base.headlessExitAfterCall = *incoming.headlessExitAfterCall;
    if (incoming.replaceAccounts) base.replaceAccounts = true;
}

// Snake-case JSON keys → QVariantMap keys consumed by
// AccountsController::applyParams.
BootAccount parseAccountObject(const QJsonObject &obj)
{
    BootAccount a;
    auto setStr = [&](const char *src, const char *dst) {
        const auto v = obj.value(QLatin1String(src));
        if (v.isString() && !v.toString().isEmpty()) {
            a.params[dst] = v.toString();
        }
    };
    auto setBool = [&](const char *src, const char *dst) {
        if (obj.contains(QLatin1String(src))) {
            a.params[dst] = obj.value(QLatin1String(src)).toBool();
        }
    };
    auto setInt = [&](const char *src, const char *dst) {
        if (obj.contains(QLatin1String(src))) {
            a.params[dst] = obj.value(QLatin1String(src)).toInt();
        }
    };

    setStr("label",        "label");
    setStr("user",         "username");
    setStr("server",       "domain");
    setStr("auth_user",    "authUser");
    setStr("realm",        "authRealm");
    setStr("display_name", "displayName");
    setStr("transport",    "transport");
    setStr("proxy",        "proxy");
    setStr("srtp",         "srtpMode");
    setStr("stun",         "stunServer");
    setStr("public_address", "publicAddress");
    setStr("voicemail",    "voicemailNumber");
    setStr("dtmf_method",  "dtmfMethod");

    setInt("register_interval_sec", "registerIntervalSec");
    setInt("keepalive_interval_sec", "keepaliveIntervalSec");
    setInt("sort_order", "sortOrder");

    setBool("session_timers_enabled", "sessionTimersEnabled");
    setBool("publish_presence_enabled", "publishPresenceEnabled");
    setBool("ice_enabled", "iceEnabled");
    setBool("hide_caller_id", "hideCallerId");
    setBool("zrtp_enabled", "zrtpEnabled");
    setBool("allow_untrusted_cert", "allowUntrustedCert");
    setBool("enabled", "enabled");
    setBool("default", "isDefault");

    if (obj.contains("codecs")) {
        const auto v = obj.value("codecs");
        if (v.isArray()) {
            QStringList list;
            for (const auto &c : v.toArray()) list << c.toString();
            a.params["codecs"] = list.join(',');
        } else if (v.isString()) {
            a.params["codecs"] = v.toString();
        }
    }

    if (obj.contains("register_on_start")) {
        a.registerOnStart = obj.value("register_on_start").toBool(true);
    }
    a.params["registerOnStartup"] = a.registerOnStart;

    if (a.params.value("authUser").toString().isEmpty()) {
        a.params["authUser"] = a.params.value("username");
    }

    if (obj.contains("password")) {
        a.password = resolvePassword(obj.value("password").toString());
    }

    if (a.params.value("label").toString().isEmpty()
        && !a.params.value("username").toString().isEmpty()
        && !a.params.value("domain").toString().isEmpty()) {
        a.params["label"] = QStringLiteral("%1@%2").arg(
            a.params.value("username").toString(),
            a.params.value("domain").toString());
    }

    return a;
}

} // namespace

BootConfig loadFromFile(const QString &path)
{
    BootConfig cfg;
    QFile f(path);
    if (!f.exists()) return cfg;
    if (!f.open(QIODevice::ReadOnly)) {
        warn(QStringLiteral("provisioning: cannot read %1: %2")
                 .arg(path, f.errorString()));
        return cfg;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        warn(QStringLiteral("provisioning: parse error in %1: %2")
                 .arg(path, err.errorString()));
        return cfg;
    }
    const auto root = doc.object();

    const int schema = root.value("schema_version").toInt(1);
    if (schema != 1) {
        warn(QStringLiteral("provisioning: %1 has unsupported schema_version "
                            "%2 (this build understands 1)")
                 .arg(path).arg(schema));
        return cfg;
    }

    if (root.contains("accounts") && root.value("accounts").isArray()) {
        for (const auto &v : root.value("accounts").toArray()) {
            if (!v.isObject()) continue;
            auto a = parseAccountObject(v.toObject());
            if (a.params.value("username").toString().isEmpty()
                || a.params.value("domain").toString().isEmpty()) {
                warn(QStringLiteral(
                    "provisioning: %1 has an account missing user or server"
                    " — dropped").arg(path));
                continue;
            }
            cfg.accounts.append(a);
        }
    }

    if (root.contains("defaults") && root.value("defaults").isObject()) {
        const auto d = root.value("defaults").toObject();
        if (d.contains("autoanswer"))       cfg.autoAnswer = d.value("autoanswer").toBool();
        if (d.contains("dnd"))              cfg.dnd = d.value("dnd").toBool();
        if (d.contains("minimize_to_tray")) cfg.minimizeToTray = d.value("minimize_to_tray").toBool();
        if (d.contains("theme"))            cfg.theme = d.value("theme").toString();
        if (d.contains("log_level"))        cfg.logLevel = d.value("log_level").toString();
        if (d.contains("log_file"))         cfg.logFile = d.value("log_file").toString();
    }

    if (root.contains("headless") && root.value("headless").isObject()) {
        const auto h = root.value("headless").toObject();
        if (h.contains("call")) {
            cfg.headlessCallUri = h.value("call").toString();
        }
        if (h.contains("auto_answer")) {
            cfg.headlessAutoAnswer = h.value("auto_answer").toBool();
        }
        if (h.contains("play_file")) {
            cfg.headlessPlayFile = h.value("play_file").toString();
        }
        if (h.contains("loop_play_file")) {
            cfg.headlessLoopPlayFile = h.value("loop_play_file").toBool();
        }
        if (h.contains("duration_sec")) {
            cfg.headlessDurationSec = h.value("duration_sec").toInt();
        }
        if (h.contains("exit_after_call")) {
            cfg.headlessExitAfterCall = h.value("exit_after_call").toBool();
        }
    }

    if (root.value("replace_accounts").toBool(false)) {
        cfg.replaceAccounts = true;
    }

    return cfg;
}

BootConfig discoverProvisioning()
{
    BootConfig cfg;
    QStringList searched;

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    searched << QStringLiteral("/etc/compactphone/provisioning.json");
#endif

    const auto appDataDirs = QStandardPaths::standardLocations(
        QStandardPaths::AppDataLocation);
    for (const auto &dir : appDataDirs) {
        searched << QDir(dir).filePath(QStringLiteral("provisioning.json"));
    }

    const QByteArray envPath = qgetenv("COMPACTPHONE_CONFIG");
    if (!envPath.isEmpty()) searched << QString::fromLocal8Bit(envPath);

    // Documented order: system, per-user, environment. Later layers override
    // scalar defaults; accounts append.
    for (const auto &path : searched) {
        mergeInto(cfg, loadFromFile(path));
    }
    return cfg;
}

QString resolvePassword(const QString &spec)
{
    if (spec.isEmpty()) return {};

    if (spec.startsWith(QStringLiteral("@env:"))) {
        const QString var = spec.mid(5);
        const QByteArray val = qgetenv(var.toUtf8().constData());
        if (val.isNull()) {
            warn(QStringLiteral("env var %1 not set; password empty").arg(var));
            return {};
        }
        return QString::fromLocal8Bit(val);
    }

    if (spec.startsWith(QStringLiteral("@file:"))) {
        const QString path = spec.mid(6);
        QFile f(path);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            warn(QStringLiteral("cannot read password file %1: %2")
                     .arg(path, f.errorString()));
            return {};
        }
        return QString::fromUtf8(f.readLine()).trimmed();
    }

    if (spec == QStringLiteral("@stdin")) {
        QTextStream in(stdin, QIODevice::ReadOnly);
        return in.readLine();
    }

    return spec; // literal
}

namespace {

bool boolOptionValue(const QString &value, bool fallback)
{
    const QString v = value.trimmed().toLower();
    if (v == QStringLiteral("0") || v == QStringLiteral("no")
        || v == QStringLiteral("false") || v == QStringLiteral("off")) {
        return false;
    }
    if (v == QStringLiteral("1") || v == QStringLiteral("yes")
        || v == QStringLiteral("true") || v == QStringLiteral("on")) {
        return true;
    }
    return fallback;
}

bool hasPort(const QString &server)
{
    if (server.startsWith(QLatin1Char('['))) {
        return server.contains(QStringLiteral("]:"));
    }
    return server.count(QLatin1Char(':')) == 1;
}

QString serverWithOptionalPort(const QString &server, const QString &port)
{
    const QString trimmedServer = server.trimmed();
    const QString trimmedPort = port.trimmed();
    if (trimmedServer.isEmpty() || trimmedPort.isEmpty() || hasPort(trimmedServer)) {
        return trimmedServer;
    }
    // Already-bracketed IPv6 literal without a port: append the port without
    // re-wrapping (otherwise "[2001:db8::1]" would become "[[2001:db8::1]]:p").
    if (trimmedServer.startsWith(QLatin1Char('['))
        && trimmedServer.endsWith(QLatin1Char(']'))) {
        return trimmedServer + QLatin1Char(':') + trimmedPort;
    }
    // Bare IPv6 literal (contains ':'): bracket it before adding the port.
    if (trimmedServer.contains(QLatin1Char(':'))) {
        return QStringLiteral("[%1]:%2").arg(trimmedServer, trimmedPort);
    }
    return trimmedServer + QLatin1Char(':') + trimmedPort;
}

void setBoolPair(const QCommandLineParser &parser,
                 QVariantMap &params,
                 const QString &positive,
                 const QString &negative,
                 const char *dst)
{
    if (parser.isSet(positive)) params[dst] = true;
    if (parser.isSet(negative)) params[dst] = false;
}

} // namespace

BootConfig parseCommandLine(const QStringList &args)
{
    QCommandLineParser parser;
    configureParser(parser);
    parser.process(args);

    // Layer 1: discover system / per-user provisioning files.
    BootConfig cfg = discoverProvisioning();

    // Layer 2: explicit --config <path>. Errors for an explicit path are
    // surfaced — we only return an empty config if loadFromFile already
    // warned about the failure.
    if (parser.isSet("config")) {
        mergeInto(cfg, loadFromFile(parser.value("config")));
    }

    // Layer 3 (highest priority) is the CLI itself; the code below populates
    // a fresh `cliCfg` then merges it last so CLI flags overwrite scalars
    // and add additional accounts on top of file-provided ones.
    BootConfig cliCfg;

    // Build the account if any SIP flag was given. A single CLI invocation
    // creates at most one account; the config file (#4) handles N accounts.
    const bool hasAccountFlags = parser.isSet("sip-server")
        || parser.isSet("sip-user")
        || parser.isSet("sip-port")
        || parser.isSet("sip-auth-user")
        || parser.isSet("sip-realm")
        || parser.isSet("sip-password")
        || parser.isSet("sip-password-file")
        || parser.isSet("sip-password-stdin")
        || parser.isSet("sip-proxy")
        || parser.isSet("sip-transport")
        || parser.isSet("sip-srtp")
        || parser.isSet("sip-display-name")
        || parser.isSet("sip-stun")
        || parser.isSet("sip-public-address")
        || parser.isSet("sip-codecs")
        || parser.isSet("sip-voicemail")
        || parser.isSet("sip-register-interval")
        || parser.isSet("sip-keepalive-interval")
        || parser.isSet("sip-session-timers")
        || parser.isSet("sip-publish-presence")
        || parser.isSet("no-sip-publish-presence")
        || parser.isSet("sip-ice")
        || parser.isSet("no-sip-ice")
        || parser.isSet("sip-hide-caller-id")
        || parser.isSet("no-sip-hide-caller-id")
        || parser.isSet("sip-zrtp")
        || parser.isSet("no-sip-zrtp")
        || parser.isSet("sip-allow-untrusted-cert")
        || parser.isSet("no-sip-allow-untrusted-cert")
        || parser.isSet("sip-dtmf-method")
        || parser.isSet("sip-enabled")
        || parser.isSet("sip-default")
        || parser.isSet("sip-sort-order")
        || parser.isSet("register-on-start")
        || parser.isSet("sip-label");

    if (hasAccountFlags) {
        BootAccount a;
        const QString server = serverWithOptionalPort(
            parser.value("sip-server"), parser.value("sip-port"));
        const QString user   = parser.value("sip-user").trimmed();

        if (server.isEmpty() || user.isEmpty()) {
            warn(QStringLiteral("--sip-server and --sip-user are both "
                                "required; ignoring CLI account."));
        } else {
            a.params["username"]   = user;
            a.params["domain"]     = server;
            a.params["authUser"]   = parser.value("sip-auth-user").trimmed();
            if (a.params["authUser"].toString().isEmpty()) {
                a.params["authUser"] = user;
            }
            if (parser.isSet("sip-realm")) {
                a.params["authRealm"] = parser.value("sip-realm").trimmed();
            }

            const QString label = parser.value("sip-label").trimmed();
            a.params["label"] = label.isEmpty()
                ? QStringLiteral("%1@%2").arg(user, server)
                : label;

            if (parser.isSet("sip-display-name")) {
                a.params["displayName"] = parser.value("sip-display-name");
            }
            if (parser.isSet("sip-proxy")) {
                a.params["proxy"] = parser.value("sip-proxy");
            }
            if (parser.isSet("sip-transport")) {
                a.params["transport"] = parser.value("sip-transport").toLower();
            }
            if (parser.isSet("sip-srtp")) {
                a.params["srtpMode"] = parser.value("sip-srtp").toLower();
            }
            if (parser.isSet("sip-stun")) {
                a.params["stunServer"] = parser.value("sip-stun");
            }
            if (parser.isSet("sip-public-address")) {
                a.params["publicAddress"] = parser.value("sip-public-address");
            }
            if (parser.isSet("sip-codecs")) {
                a.params["codecs"] = parser.value("sip-codecs");
            }
            if (parser.isSet("sip-voicemail")) {
                a.params["voicemailNumber"] = parser.value("sip-voicemail");
            }
            if (parser.isSet("sip-register-interval")) {
                a.params["registerIntervalSec"] =
                    parser.value("sip-register-interval").toInt();
            }
            if (parser.isSet("sip-keepalive-interval")) {
                a.params["keepaliveIntervalSec"] =
                    parser.value("sip-keepalive-interval").toInt();
            }
            if (parser.isSet("sip-session-timers")) {
                a.params["sessionTimersEnabled"] = boolOptionValue(
                    parser.value("sip-session-timers"), true);
            }
            setBoolPair(parser, a.params, QStringLiteral("sip-publish-presence"),
                        QStringLiteral("no-sip-publish-presence"),
                        "publishPresenceEnabled");
            setBoolPair(parser, a.params, QStringLiteral("sip-ice"),
                        QStringLiteral("no-sip-ice"), "iceEnabled");
            setBoolPair(parser, a.params, QStringLiteral("sip-hide-caller-id"),
                        QStringLiteral("no-sip-hide-caller-id"),
                        "hideCallerId");
            setBoolPair(parser, a.params, QStringLiteral("sip-zrtp"),
                        QStringLiteral("no-sip-zrtp"), "zrtpEnabled");
            setBoolPair(parser, a.params,
                        QStringLiteral("sip-allow-untrusted-cert"),
                        QStringLiteral("no-sip-allow-untrusted-cert"),
                        "allowUntrustedCert");
            if (parser.isSet("sip-dtmf-method")) {
                a.params["dtmfMethod"] =
                    parser.value("sip-dtmf-method").toLower();
            }
            if (parser.isSet("sip-enabled")) {
                a.params["enabled"] =
                    boolOptionValue(parser.value("sip-enabled"), true);
            }
            if (parser.isSet("sip-default")) {
                a.params["isDefault"] = true;
            }
            if (parser.isSet("sip-sort-order")) {
                a.params["sortOrder"] =
                    parser.value("sip-sort-order").toInt();
            }

            // Password source priority: --sip-password-stdin > --sip-password-file
            // > --sip-password literal > empty.
            if (parser.isSet("sip-password-stdin")) {
                a.password = resolvePassword(QStringLiteral("@stdin"));
            } else if (parser.isSet("sip-password-file")) {
                a.password = resolvePassword(
                    QStringLiteral("@file:") + parser.value("sip-password-file"));
            } else if (parser.isSet("sip-password")) {
                warn(QStringLiteral(
                    "--sip-password leaks via `ps`; prefer "
                    "--sip-password-file or --sip-password-stdin."));
                a.password = resolvePassword(parser.value("sip-password"));
            }

            if (parser.isSet("register-on-start")) {
                a.registerOnStart =
                    boolOptionValue(parser.value("register-on-start"), true);
            }
            a.params["registerOnStartup"] = a.registerOnStart;

            cliCfg.accounts.append(a);
        }
    }

    if (parser.isSet("replace-account"))   cliCfg.replaceAccounts = true;
    if (parser.isSet("autoanswer"))        cliCfg.autoAnswer = true;
    if (parser.isSet("auto-answer"))       cliCfg.headlessAutoAnswer = true;
    if (parser.isSet("dnd"))               cliCfg.dnd = true;
    if (parser.isSet("minimize-to-tray"))  cliCfg.minimizeToTray = true;
    if (parser.isSet("theme"))             cliCfg.theme = parser.value("theme");
    if (parser.isSet("log-level"))         cliCfg.logLevel = parser.value("log-level");
    if (parser.isSet("log-file"))          cliCfg.logFile = parser.value("log-file");
    if (parser.isSet("call"))              cliCfg.headlessCallUri = parser.value("call");
    if (parser.isSet("play-file"))         cliCfg.headlessPlayFile = parser.value("play-file");
    if (parser.isSet("loop-play-file"))    cliCfg.headlessLoopPlayFile = true;
    if (parser.isSet("duration-sec"))      cliCfg.headlessDurationSec = parser.value("duration-sec").toInt();
    if (parser.isSet("exit-after-call"))   cliCfg.headlessExitAfterCall = true;

    mergeInto(cfg, cliCfg);
    return cfg;
}

} // namespace compactphone::bootconfig

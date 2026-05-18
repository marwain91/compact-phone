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
        "flags can also be supplied via a TOML/JSON provisioning file passed\n"
        "with --config. CLI overrides take precedence over the file.\n");

    parser.addOptions({
        // --- account creation ---
        {"sip-server",
         "SIP server hostname or IP. Becomes both the registrar and realm.",
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
        {"sip-codecs",
         "Comma-separated codec priority list, e.g. opus,PCMA,PCMU.",
         "list"},
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
         "Read additional provisioning settings from this TOML/JSON file. "
         "CLI flags override file values.",
         "path"},
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

    setStr("label",        "label");
    setStr("user",         "username");
    setStr("server",       "domain");
    setStr("auth_user",    "authUser");
    setStr("display_name", "displayName");
    setStr("transport",    "transport");
    setStr("srtp",         "srtpMode");
    setStr("stun",         "stunServer");
    setStr("voicemail",    "voicemailNumber");

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

    if (root.value("replace_accounts").toBool(false)) {
        cfg.replaceAccounts = true;
    }

    return cfg;
}

BootConfig discoverProvisioning()
{
    BootConfig cfg;
    QStringList searched;

    const QByteArray envPath = qgetenv("COMPACTPHONE_CONFIG");
    if (!envPath.isEmpty()) searched << QString::fromLocal8Bit(envPath);

#if defined(Q_OS_LINUX) || defined(Q_OS_UNIX)
    searched << QStringLiteral("/etc/compactphone/provisioning.json");
#endif

    const auto appDataDirs = QStandardPaths::standardLocations(
        QStandardPaths::AppDataLocation);
    for (const auto &dir : appDataDirs) {
        searched << QDir(dir).filePath(QStringLiteral("provisioning.json"));
    }

    // Earliest-discovered first; later layers override scalars.
    for (auto it = searched.crbegin(); it != searched.crend(); ++it) {
        mergeInto(cfg, loadFromFile(*it));
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
        || parser.isSet("sip-password")
        || parser.isSet("sip-password-file")
        || parser.isSet("sip-password-stdin");

    if (hasAccountFlags) {
        BootAccount a;
        const QString server = parser.value("sip-server").trimmed();
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

            const QString label = parser.value("sip-label").trimmed();
            a.params["label"] = label.isEmpty()
                ? QStringLiteral("%1@%2").arg(user, server)
                : label;

            if (parser.isSet("sip-display-name")) {
                a.params["displayName"] = parser.value("sip-display-name");
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
            if (parser.isSet("sip-codecs")) {
                a.params["codecs"] = parser.value("sip-codecs");
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
                const QString v = parser.value("register-on-start").toLower();
                a.registerOnStart = !(v == "0" || v == "no" || v == "false");
            }
            a.params["registerOnStartup"] = a.registerOnStart;

            cliCfg.accounts.append(a);
        }
    }

    if (parser.isSet("replace-account"))   cliCfg.replaceAccounts = true;
    if (parser.isSet("autoanswer"))        cliCfg.autoAnswer = true;
    if (parser.isSet("dnd"))               cliCfg.dnd = true;
    if (parser.isSet("minimize-to-tray"))  cliCfg.minimizeToTray = true;
    if (parser.isSet("theme"))             cliCfg.theme = parser.value("theme");
    if (parser.isSet("log-level"))         cliCfg.logLevel = parser.value("log-level");
    if (parser.isSet("log-file"))          cliCfg.logFile = parser.value("log-file");

    mergeInto(cfg, cliCfg);
    return cfg;
}

} // namespace compactphone::bootconfig

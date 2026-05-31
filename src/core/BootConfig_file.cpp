#include "BootConfig.h"
#include "BootConfig_internal.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QStandardPaths>
#include <QtGlobal>

namespace compactphone::bootconfig {

namespace detail {

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

} // namespace detail

namespace {

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
        detail::warn(QStringLiteral("provisioning: cannot read %1: %2")
                         .arg(path, f.errorString()));
        return cfg;
    }

    QJsonParseError err{};
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        detail::warn(QStringLiteral("provisioning: parse error in %1: %2")
                         .arg(path, err.errorString()));
        return cfg;
    }
    const auto root = doc.object();

    const int schema = root.value("schema_version").toInt(1);
    if (schema != 1) {
        detail::warn(QStringLiteral("provisioning: %1 has unsupported schema_version "
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
                detail::warn(QStringLiteral(
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
        detail::mergeInto(cfg, loadFromFile(path));
    }
    return cfg;
}

} // namespace compactphone::bootconfig

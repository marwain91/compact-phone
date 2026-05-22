#pragma once

#include <QCommandLineParser>
#include <QString>
#include <QStringList>
#include <QVariantMap>

#include <optional>

namespace compactphone {

// One account's worth of provisioning data, expressed in QVariantMap form so
// it can be handed straight to PhoneController::addAccount / updateAccount.
struct BootAccount {
    QVariantMap params;          // keys match AccountsController::applyParams
    QString password;            // resolved (may be empty)
    bool registerOnStart = true;
};

struct BootConfig {
    QList<BootAccount> accounts;

    // Global settings. nullopt = don't touch.
    std::optional<bool> autoAnswer;
    std::optional<bool> dnd;
    std::optional<bool> minimizeToTray;
    std::optional<QString> theme;
    std::optional<QString> logLevel;
    std::optional<QString> logFile;

    // Headless SIP test mode. Used by compactphone-headless; ignored by the
    // GUI executable except for shared --help/parse support.
    std::optional<QString> headlessCallUri;
    std::optional<bool> headlessAutoAnswer;
    std::optional<QString> headlessPlayFile;
    std::optional<bool> headlessLoopPlayFile;
    std::optional<int> headlessDurationSec;
    std::optional<bool> headlessExitAfterCall;

    // True if --replace-account was passed; existing accounts matching
    // (username, domain) are updated instead of duplicated.
    bool replaceAccounts = false;

    bool empty() const {
        return accounts.isEmpty() && !autoAnswer && !dnd && !minimizeToTray
            && !theme && !logLevel && !logFile && !headlessCallUri
            && !headlessAutoAnswer && !headlessPlayFile
            && !headlessLoopPlayFile && !headlessDurationSec
            && !headlessExitAfterCall;
    }
};

namespace bootconfig {

// Build a QCommandLineParser pre-populated with every flag we support.
// Reused by `parseCommandLine` and by tests / --help docs.
void configureParser(QCommandLineParser &parser);

// Parse argv into a BootConfig. Emits warnings to stderr (e.g., for
// --sip-password literals) but never aborts on partial data — invalid
// individual accounts are dropped, valid ones still applied.
//
// Discovery order (each found layer merges into the result, later wins):
//   1. provisioning file at every searched path (--config, env var,
//      /etc/compactphone/, AppData) — see `loadFromFile`
//   2. CLI flags from `args`
BootConfig parseCommandLine(const QStringList &args);

// Load a single provisioning file (JSON). Returns an empty config if the
// file is missing or malformed; warnings go to stderr. Errors in individual
// account entries are dropped without aborting the whole file.
BootConfig loadFromFile(const QString &path);

// Run the documented discovery chain (skipping `cliConfigPath` if set, which
// is loaded separately so its errors surface clearly). Returns a single
// merged BootConfig.
BootConfig discoverProvisioning();

// Resolve a password spec: literal, "@file:/path", "@env:VAR", "@stdin",
// or the empty string. Returns the resolved password (may be empty).
QString resolvePassword(const QString &spec);

} // namespace bootconfig
} // namespace compactphone

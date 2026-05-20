#pragma once

#include <QObject>
#include <QString>
#include <QVariantMap>

class QNetworkAccessManager;

namespace compactphone::provisioning {

// Abstract base for an "auto-provisioning" backend. A provider takes a small
// set of user-supplied credentials (host + username + password) and resolves
// them into a fully-formed account params QVariantMap that
// AccountsController::addAccount() understands.
//
// All work runs asynchronously on the caller's event loop. Subclasses MUST
// emit exactly one of provisioningSucceeded / provisioningFailed per call to
// provision().
//
// Adding a new provider:
//   1. Subclass Provider, give it a unique id() (kebab-case, e.g. "daktela").
//   2. Implement provision() to drive the backend's auth + account-discovery
//      flow and emit signals.
//   3. Register it via Registry::registerBuiltins() (or add a new factory).
class Provider : public QObject {
    Q_OBJECT
public:
    explicit Provider(QObject *parent = nullptr) : QObject(parent) {}

    // Stable machine-readable id, e.g. "daktela". Used as the discriminator
    // in PhoneController::provisionWithProvider().
    virtual QString id() const = 0;

    // Localized human-readable name shown in the UI, e.g. "Daktela".
    virtual QString displayName() const = 0;

    // What the host field should hint at, e.g. "https://acme.daktela.com".
    virtual QString hostPlaceholder() const { return {}; }

    // Optional Qt resource URL for the provider's wordmark, used in the
    // footer + wizard header. markPathDark() falls back to markPath() when
    // a provider only ships one variant.
    virtual QString markPath() const { return {}; }
    virtual QString markPathDark() const { return markPath(); }

    // Inject a QNAM for tests; otherwise the provider creates its own.
    virtual void setNetworkAccessManager(QNetworkAccessManager *) {}

    // Kick off the password flow. Subclasses must always emit either
    // provisioningSucceeded or provisioningFailed in response.
    virtual void provision(const QString &host,
                           const QString &username,
                           const QString &password) = 0;

    // Kick off provisioning from an access token the user generated
    // themselves in the provider's web UI (e.g. Daktela's Account →
    // API tokens). Default = unsupported.
    virtual void provisionWithToken(const QString &host,
                                    const QString &accessToken)
    {
        Q_UNUSED(host);
        Q_UNUSED(accessToken);
        emit provisioningFailed(
            tr("This provider does not support token-based sign-in."));
    }

    // Probe the host to confirm it's reachable and is the kind of
    // backend this provider expects (typo guard). Subclasses must
    // always emit either authMethodsDiscovered or authMethodsFailed.
    //
    // Each entry in the emitted list is a QVariantMap with:
    //   id            stable machine id, e.g. "password" / "token"
    //   displayName   localized button label
    //   kind          "password" | "token"
    //   openUrl       for token: a URL the wizard opens in the system
    //                 browser so the user can sign in and create a
    //                 personal access token
    //   instructions  short text shown next to the token paste field
    virtual void discoverAuthMethods(const QString &host) = 0;

signals:
    // Free-form stage string for wizard UIs. Common values:
    //   "logging-in" | "fetching-user" | "fetching-extension" | "done"
    // Subclasses are free to add their own.
    void progress(QString stage);

    // params is suitable for AccountsController::addAccount; the "password"
    // key carries the SIP secret that will go to the keychain.
    void provisioningSucceeded(QVariantMap params);

    // host + accessToken (or equivalent) for the UI to persist if it wants
    // to drive refresh flows later. Optional — providers without tokens may
    // skip this signal.
    void tokenIssued(QString host, QString accessToken);

    void provisioningFailed(QString error);

    // host is echoed back so a wizard can ignore stale responses.  methods
    // is a QVariantList<QVariantMap> (see discoverAuthMethods()).
    void authMethodsDiscovered(QString host, QVariantList methods);
    void authMethodsFailed(QString host, QString error);
};

} // namespace compactphone::provisioning

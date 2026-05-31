#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <functional>
#include <memory>

namespace compactphone::provisioning { class Registry; }

namespace compactphone {

// Auto-provisioning surface: enumerate providers, discover auth methods, and
// provision an account by password or SSO token. Exposed to QML as
// PhoneController.provisioning. Owns the provider Registry and re-emits each
// provider's lifecycle signals. On success it creates the account via an
// AddAccountFn callback (so it doesn't depend on AccountsController directly)
// and surfaces notices via a NoticeSink (mirrors CallsController).
class ProvisioningController : public QObject {
    Q_OBJECT
public:
    using AddAccountFn = std::function<int(const QVariantMap &)>;
    using NoticeSink = std::function<void(const QString &, int)>;

    ProvisioningController(AddAccountFn addAccount, NoticeSink noticeSink,
                           QObject *parent = nullptr);
    ~ProvisioningController() override;

    // {"id", "displayName", "hostPlaceholder"} per built-in provider.
    Q_INVOKABLE QVariantList providers() const;

    Q_INVOKABLE void provision(const QString &providerId, const QString &host,
                               const QString &username, const QString &password);
    Q_INVOKABLE void provisionWithToken(const QString &providerId,
                                        const QString &host,
                                        const QString &accessToken);
    Q_INVOKABLE void discoverAuthMethods(const QString &providerId,
                                         const QString &host);

signals:
    void provisioningProgress(QString providerId, QString stage);
    void provisioningFailed(QString providerId, QString error);
    void accountProvisioned(QString providerId, int accountId);
    void authMethodsDiscovered(QString providerId, QString host, QVariantList methods);
    void authMethodsFailed(QString providerId, QString host, QString error);

private:
    std::unique_ptr<provisioning::Registry> m_registry;
    AddAccountFn m_addAccount;
    NoticeSink m_noticeSink;
};

} // namespace compactphone

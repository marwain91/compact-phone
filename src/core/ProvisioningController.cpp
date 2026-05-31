#include "ProvisioningController.h"

#include "provisioning/Provider.h"
#include "provisioning/Registry.h"

#include <utility>

namespace compactphone {

ProvisioningController::ProvisioningController(AddAccountFn addAccount,
                                              NoticeSink noticeSink,
                                              QObject *parent)
    : QObject(parent),
      m_registry(std::make_unique<provisioning::Registry>()),
      m_addAccount(std::move(addAccount)),
      m_noticeSink(std::move(noticeSink))
{
    for (const auto &id : m_registry->ids()) {
        auto *p = m_registry->find(id);
        if (!p) continue;
        connect(p, &provisioning::Provider::progress,
                this, [this, id](const QString &stage) {
            emit provisioningProgress(id, stage);
        });
        connect(p, &provisioning::Provider::provisioningFailed,
                this, [this, id](const QString &error) {
            if (m_noticeSink) m_noticeSink(tr("Sign-in failed: %1").arg(error), 5000);
            emit provisioningFailed(id, error);
        });
        connect(p, &provisioning::Provider::provisioningSucceeded,
                this, [this, id](const QVariantMap &params) {
            const int newId = m_addAccount ? m_addAccount(params) : -1;
            if (newId < 0) {
                const QString reason = tr("Could not save the new account.");
                if (m_noticeSink) m_noticeSink(tr("Sign-in failed: %1").arg(reason), 5000);
                emit provisioningFailed(id, reason);
                return;
            }
            if (m_noticeSink) m_noticeSink(tr("Account added"), 4000);
            emit accountProvisioned(id, newId);
        });
        connect(p, &provisioning::Provider::authMethodsDiscovered,
                this, [this, id](const QString &host, const QVariantList &methods) {
            emit authMethodsDiscovered(id, host, methods);
        });
        connect(p, &provisioning::Provider::authMethodsFailed,
                this, [this, id](const QString &host, const QString &error) {
            emit authMethodsFailed(id, host, error);
        });
    }
}

ProvisioningController::~ProvisioningController() = default;

QVariantList ProvisioningController::providers() const
{
    return m_registry ? m_registry->descriptors() : QVariantList{};
}

void ProvisioningController::provision(const QString &providerId,
                                       const QString &host,
                                       const QString &username,
                                       const QString &password)
{
    if (!m_registry) return;
    auto *p = m_registry->find(providerId);
    if (!p) {
        const QString msg = tr("Unknown provisioning provider: %1").arg(providerId);
        if (m_noticeSink) m_noticeSink(msg, 5000);
        emit provisioningFailed(providerId, msg);
        return;
    }
    p->provision(host, username, password);
}

void ProvisioningController::provisionWithToken(const QString &providerId,
                                                const QString &host,
                                                const QString &accessToken)
{
    if (!m_registry) return;
    auto *p = m_registry->find(providerId);
    if (!p) {
        const QString msg = tr("Unknown provisioning provider: %1").arg(providerId);
        if (m_noticeSink) m_noticeSink(msg, 5000);
        emit provisioningFailed(providerId, msg);
        return;
    }
    p->provisionWithToken(host, accessToken);
}

void ProvisioningController::discoverAuthMethods(const QString &providerId,
                                                 const QString &host)
{
    if (!m_registry) return;
    auto *p = m_registry->find(providerId);
    if (!p) {
        emit authMethodsFailed(providerId, host,
                               tr("Unknown provisioning provider: %1").arg(providerId));
        return;
    }
    p->discoverAuthMethods(host);
}

} // namespace compactphone

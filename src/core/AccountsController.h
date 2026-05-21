#pragma once

#include "Account.h"
#include "AccountsManager.h"

#include <QAbstractListModel>
#include <QObject>
#include <QVariantMap>

#include <unordered_map>

namespace compactphone::models { class AccountsModel; }
namespace compactphone::sip {
class AccountsManager;
class SipEngine;
}

namespace compactphone {

class AccountsController : public QObject {
    Q_OBJECT
public:
    explicit AccountsController(sip::AccountsManager *accounts,
                                models::AccountsModel *model,
                                sip::SipEngine *engine = nullptr,
                                QObject *parent = nullptr);
    ~AccountsController() override;

    QAbstractListModel *model() const;

    int addAccount(const QVariantMap &params);
    bool removeAccount(int accountId);
    bool updateAccount(int accountId, const QVariantMap &params);
    QVariantMap accountSnapshot(int accountId) const;
    bool setDefaultAccount(int accountId);
    bool setAccountEnabled(int accountId, bool enabled);

    int registeredAccountCount() const { return m_registeredAccountCount; }
    int activeAccountId() const;
    void setActiveAccountId(int id);

signals:
    void registeredAccountCountChanged();
    void activeAccountIdChanged();
    // Fired when an account transitions into the Failed registration state,
    // so PhoneController can surface the reason via its snackbar channel.
    // Pre-formatted message; account label included.
    void registrationFailed(QString message);

private:
    sip::AccountsManager *m_accounts = nullptr;
    models::AccountsModel *m_model = nullptr;
    sip::SipEngine *m_engine = nullptr;
    int m_registeredAccountCount = 0;
    int m_activeAccountId = -1;
    // Per-account last observed state, so we only emit registrationFailed
    // on the edge into Failed (not on every onRegState callback while the
    // account stays failed and PJSIP retries).
    std::unordered_map<int, sip::RegistrationState> m_lastState;

    void refreshModel();
    void refreshRegisteredAccountCount();
    void pushNetworkAndCodecSettings();
};

} // namespace compactphone

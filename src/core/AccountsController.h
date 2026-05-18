#pragma once

#include "Account.h"

#include <QAbstractListModel>
#include <QObject>
#include <QVariantMap>

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

private:
    sip::AccountsManager *m_accounts = nullptr;
    models::AccountsModel *m_model = nullptr;
    sip::SipEngine *m_engine = nullptr;
    int m_registeredAccountCount = 0;
    int m_activeAccountId = -1;

    void refreshModel();
    void refreshRegisteredAccountCount();
    void pushNetworkAndCodecSettings();
};

} // namespace compactphone

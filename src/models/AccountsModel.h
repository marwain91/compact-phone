#pragma once

#include "core/Account.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class AccountsManager; }

namespace compactphone::models {

class AccountsModel : public QAbstractListModel {
    Q_OBJECT
    // Reactive row count for QML. rowCount() alone is a plain method call, so
    // bindings like `visible: accounts.rowCount() > 1` never re-evaluate when
    // accounts load after the view is built. Bind to `count` instead.
    Q_PROPERTY(int count READ rowCount NOTIFY countChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        UsernameRole,
        DomainRole,
        TransportRole,
        IsDefaultRole,
        EnabledRole,
        RegistrationStateRole,
        RegistrationErrorRole,
        LabelRole,
        ProviderRole,
    };

    explicit AccountsModel(sip::AccountsManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

    // Emit dataChanged for one account's registration roles without a full
    // reset. Registration state is read live from the manager (not stored in
    // the snapshot), and reg-state changes are frequent (every registration
    // event / network flap), so a reset here would needlessly tear down every
    // account delegate.
    void notifyRegistrationChanged(int accountId);

signals:
    void countChanged();

private:
    sip::AccountsManager *m_mgr;
    std::vector<sip::Account> m_snapshot;
};

} // namespace compactphone::models

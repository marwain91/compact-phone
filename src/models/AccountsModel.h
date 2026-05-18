#pragma once

#include "core/Account.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class AccountsManager; }

namespace compactphone::models {

class AccountsModel : public QAbstractListModel {
    Q_OBJECT
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
        LabelRole,
    };

    explicit AccountsModel(sip::AccountsManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    sip::AccountsManager *m_mgr;
    std::vector<sip::Account> m_snapshot;
};

} // namespace compactphone::models

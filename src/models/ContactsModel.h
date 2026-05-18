#pragma once

#include "core/Contact.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class ContactsManager; }

namespace compactphone::models {

class ContactsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        DisplayNameRole,
        SipUriRole,
        PhoneRole,
        FavoriteRole,
    };

    explicit ContactsModel(sip::ContactsManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    sip::ContactsManager *m_mgr;
    std::vector<sip::Contact> m_snapshot;
};

} // namespace compactphone::models

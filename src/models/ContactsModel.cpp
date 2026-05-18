#include "ContactsModel.h"

#include "core/ContactsManager.h"

namespace compactphone::models {

ContactsModel::ContactsModel(sip::ContactsManager *mgr, QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    refresh();
}

int ContactsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant ContactsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_snapshot.size())) return {};
    const auto &c = m_snapshot[index.row()];
    switch (role) {
    case IdRole:          return c.id;
    case DisplayNameRole: return QString::fromStdString(c.displayName);
    case SipUriRole:      return QString::fromStdString(c.sipUri);
    case PhoneRole:       return QString::fromStdString(c.phone);
    case FavoriteRole:    return c.favorite;
    }
    return {};
}

QHash<int, QByteArray> ContactsModel::roleNames() const
{
    return {
        {IdRole, "contactId"},
        {DisplayNameRole, "displayName"},
        {SipUriRole, "sipUri"},
        {PhoneRole, "phone"},
        {FavoriteRole, "favorite"},
    };
}

void ContactsModel::refresh()
{
    beginResetModel();
    m_snapshot = m_mgr ? m_mgr->list() : std::vector<sip::Contact>{};
    endResetModel();
}

} // namespace compactphone::models

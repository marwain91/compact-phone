#include "AccountsModel.h"

#include "core/AccountsManager.h"

namespace compactphone::models {

namespace {

QString regStateString(sip::RegistrationState s)
{
    switch (s) {
    case sip::RegistrationState::Unregistered: return QStringLiteral("unregistered");
    case sip::RegistrationState::Registering:  return QStringLiteral("registering");
    case sip::RegistrationState::Registered:   return QStringLiteral("registered");
    case sip::RegistrationState::Failed:       return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

} // namespace

AccountsModel::AccountsModel(sip::AccountsManager *mgr, QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    refresh();
}

int AccountsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant AccountsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_snapshot.size())) {
        return {};
    }
    const auto &a = m_snapshot[index.row()];
    switch (role) {
    case IdRole:          return a.id;
    case DisplayNameRole: return QString::fromStdString(a.displayName);
    case UsernameRole:    return QString::fromStdString(a.username);
    case DomainRole:      return QString::fromStdString(a.domain);
    case TransportRole:   return QString::fromUtf8(sip::transportToString(a.transport));
    case IsDefaultRole:   return a.isDefault;
    case EnabledRole:     return a.enabled;
    case RegistrationStateRole:
        return regStateString(m_mgr ? m_mgr->stateOf(a.id)
                                    : sip::RegistrationState::Unregistered);
    case LabelRole: {
        const auto &l = a.label.empty() ? a.displayName : a.label;
        return QString::fromStdString(l);
    }
    }
    return {};
}

QHash<int, QByteArray> AccountsModel::roleNames() const
{
    return {
        {IdRole, "accountId"},
        {DisplayNameRole, "displayName"},
        {UsernameRole, "username"},
        {DomainRole, "domain"},
        {TransportRole, "transport"},
        {IsDefaultRole, "isDefault"},
        {EnabledRole, "enabled"},
        {RegistrationStateRole, "registrationState"},
        {LabelRole, "label"},
    };
}

void AccountsModel::refresh()
{
    beginResetModel();
    m_snapshot = m_mgr ? m_mgr->list() : std::vector<sip::Account>{};
    endResetModel();
}

} // namespace compactphone::models

#include "HistoryModel.h"

#include "core/HistoryManager.h"

namespace compactphone::models {

HistoryModel::HistoryModel(sip::HistoryManager *mgr, QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    refresh();
}

int HistoryModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant HistoryModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0
        || index.row() >= static_cast<int>(m_snapshot.size())) return {};
    const auto &e = m_snapshot[index.row()];
    switch (role) {
    case IdRole:                return e.id;
    case AccountIdRole:         return e.accountId;
    case DirectionRole:         return QString::fromUtf8(sip::callDirectionToString(e.direction));
    case RemoteUriRole:         return QString::fromStdString(e.remoteUri);
    case RemoteDisplayNameRole: return QString::fromStdString(e.remoteDisplayName);
    case StartedAtRole:         return QVariant::fromValue(e.startedAt);
    case DurationMsRole:        return QVariant::fromValue(e.durationMs);
    case EndReasonRole:         return QString::fromStdString(e.endReason);
    }
    return {};
}

QHash<int, QByteArray> HistoryModel::roleNames() const
{
    return {
        {IdRole, "historyId"},
        {AccountIdRole, "accountId"},
        {DirectionRole, "direction"},
        {RemoteUriRole, "remoteUri"},
        {RemoteDisplayNameRole, "remoteDisplayName"},
        {StartedAtRole, "startedAt"},
        {DurationMsRole, "durationMs"},
        {EndReasonRole, "endReason"},
    };
}

void HistoryModel::refresh()
{
    beginResetModel();
    m_snapshot = m_mgr ? m_mgr->list() : std::vector<sip::HistoryEntry>{};
    endResetModel();
}

} // namespace compactphone::models

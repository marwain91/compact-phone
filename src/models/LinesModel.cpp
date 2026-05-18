#include "LinesModel.h"

#include "core/LinesManager.h"

namespace compactphone::models {

LinesModel::LinesModel(sip::LinesManager *mgr, QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    if (m_mgr) {
        connect(m_mgr, &sip::LinesManager::linesChanged,
                this, &LinesModel::refresh);
    }
    refresh();
}

int LinesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant LinesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_snapshot.size())) {
        return {};
    }
    const auto &l = m_snapshot[index.row()];
    switch (role) {
    case IdRole:        return l.id;
    case AccountIdRole: return l.accountId;
    case UriRole:       return QString::fromStdString(l.uri);
    case LabelRole:     return QString::fromStdString(l.label);
    case StateRole:     return QString::fromUtf8(sip::lineStateToString(l.state));
    }
    return {};
}

QHash<int, QByteArray> LinesModel::roleNames() const
{
    return {
        {IdRole, "lineId"},
        {AccountIdRole, "accountId"},
        {UriRole, "uri"},
        {LabelRole, "label"},
        {StateRole, "state"},
    };
}

void LinesModel::refresh()
{
    beginResetModel();
    m_snapshot = m_mgr ? m_mgr->list() : std::vector<sip::WatchedLine>{};
    endResetModel();
}

} // namespace compactphone::models

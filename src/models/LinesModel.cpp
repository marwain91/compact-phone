#include "LinesModel.h"

#include "core/LinesManager.h"

#include <algorithm>

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
    // Incremental diff so QML delegates survive a presence change: linesChanged
    // fires on every BLF NOTIFY, and a full reset would tear down and rebuild
    // every row's delegate just to flip one line's state dot.
    auto next = m_mgr ? m_mgr->list() : std::vector<sip::WatchedLine>{};

    // Step 1 — remove rows no longer present (by id).
    for (int i = static_cast<int>(m_snapshot.size()) - 1; i >= 0; --i) {
        const auto id = m_snapshot[i].id;
        const bool stillThere = std::any_of(
            next.begin(), next.end(),
            [id](const sip::WatchedLine &l) { return l.id == id; });
        if (!stillThere) {
            beginRemoveRows({}, i, i);
            m_snapshot.erase(m_snapshot.begin() + i);
            endRemoveRows();
        }
    }

    // Step 2 — update in place (dataChanged on changed roles) or append.
    for (const auto &l : next) {
        auto it = std::find_if(
            m_snapshot.begin(), m_snapshot.end(),
            [&l](const sip::WatchedLine &x) { return x.id == l.id; });
        if (it == m_snapshot.end()) {
            const int row = static_cast<int>(m_snapshot.size());
            beginInsertRows({}, row, row);
            m_snapshot.push_back(l);
            endInsertRows();
        } else {
            const int row = static_cast<int>(it - m_snapshot.begin());
            QList<int> changed;
            if (it->accountId != l.accountId) changed << AccountIdRole;
            if (it->uri != l.uri)             changed << UriRole;
            if (it->label != l.label)         changed << LabelRole;
            if (it->state != l.state)         changed << StateRole;
            *it = l;
            if (!changed.isEmpty()) {
                const auto idx = index(row);
                emit dataChanged(idx, idx, changed);
            }
        }
    }
}

} // namespace compactphone::models

#include "CallsModel.h"

#include "core/CallManager.h"

#include <algorithm>

namespace compactphone::models {

CallsModel::CallsModel(sip::CallManager *cm, QObject *parent)
    : QAbstractListModel(parent), m_cm(cm)
{
    refresh();
}

int CallsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant CallsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_snapshot.size())) {
        return {};
    }
    const auto &e = m_snapshot[index.row()];
    switch (role) {
    case IdRole:                return e.id;
    case AccountIdRole:         return e.accountId;
    case RemoteUriRole:         return QString::fromStdString(e.remoteUri);
    case RemoteDisplayNameRole: return QString::fromStdString(e.remoteDisplayName);
    case StateRole:             return QString::fromUtf8(sip::callStateToCString(e.state));
    case HeldRole:              return e.held;
    case MutedRole:             return e.muted;
    case RecordingRole:         return e.recording;
    case DirectionRole:         return QString::fromUtf8(sip::callDirectionToString(e.direction));
    }
    return {};
}

QHash<int, QByteArray> CallsModel::roleNames() const
{
    return {
        {IdRole, "callId"},
        {AccountIdRole, "accountId"},
        {RemoteUriRole, "remoteUri"},
        {RemoteDisplayNameRole, "remoteDisplayName"},
        {StateRole, "state"},
        {HeldRole, "held"},
        {MutedRole, "muted"},
        {RecordingRole, "recording"},
        {DirectionRole, "direction"},
    };
}

void CallsModel::refresh()
{
    // Incremental diff so QML delegates keep their local state (e.g. the
    // in-call DTMF keypad toggle) when call fields like held/muted change.
    auto next = m_cm ? m_cm->snapshot() : std::vector<sip::CallEntry>{};

    // Step 1 — remove rows present in m_snapshot but not in next.
    for (int i = static_cast<int>(m_snapshot.size()) - 1; i >= 0; --i) {
        const auto id = m_snapshot[i].id;
        const bool stillThere = std::any_of(
            next.begin(), next.end(),
            [id](const sip::CallEntry &e) { return e.id == id; });
        if (!stillThere) {
            beginRemoveRows({}, i, i);
            m_snapshot.erase(m_snapshot.begin() + i);
            endRemoveRows();
        }
    }

    // Step 2 — for each next entry, update in place if it already exists
    // (emit dataChanged on changed roles only), or append.
    for (const auto &e : next) {
        auto it = std::find_if(
            m_snapshot.begin(), m_snapshot.end(),
            [&e](const sip::CallEntry &x) { return x.id == e.id; });
        if (it == m_snapshot.end()) {
            const int row = static_cast<int>(m_snapshot.size());
            beginInsertRows({}, row, row);
            m_snapshot.push_back(e);
            endInsertRows();
        } else {
            const int row = static_cast<int>(it - m_snapshot.begin());
            QList<int> changed;
            if (it->accountId != e.accountId)               changed << AccountIdRole;
            if (it->remoteUri != e.remoteUri)               changed << RemoteUriRole;
            if (it->remoteDisplayName != e.remoteDisplayName) changed << RemoteDisplayNameRole;
            if (it->state != e.state)                       changed << StateRole;
            if (it->held != e.held)                         changed << HeldRole;
            if (it->muted != e.muted)                       changed << MutedRole;
            if (it->recording != e.recording)               changed << RecordingRole;
            if (it->direction != e.direction)               changed << DirectionRole;
            *it = e;
            if (!changed.isEmpty()) {
                const auto idx = index(row);
                emit dataChanged(idx, idx, changed);
            }
        }
    }
}

} // namespace compactphone::models

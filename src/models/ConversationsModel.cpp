#include "ConversationsModel.h"

#include "core/MessagesManager.h"

namespace compactphone::models {

ConversationsModel::ConversationsModel(sip::MessagesManager *mgr,
                                       QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    if (m_mgr) {
        connect(m_mgr, &sip::MessagesManager::messagesChanged,
                this, &ConversationsModel::refresh);
    }
    refresh();
}

int ConversationsModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_rows.size());
}

QVariant ConversationsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_rows.size())) {
        return {};
    }
    const auto &r = m_rows[index.row()];
    switch (role) {
    case PeerRole:            return r.peer;
    case LastBodyRole:        return r.lastBody;
    case LastDirectionRole:   return r.lastDirection;
    case LastCreatedAtMsRole: return r.lastCreatedAtMs;
    case UnreadRole:          return r.unread;
    }
    return {};
}

QHash<int, QByteArray> ConversationsModel::roleNames() const
{
    return {
        {PeerRole, "peer"},
        {LastBodyRole, "lastBody"},
        {LastDirectionRole, "lastDirection"},
        {LastCreatedAtMsRole, "lastCreatedAtMs"},
        {UnreadRole, "unread"},
    };
}

void ConversationsModel::refresh()
{
    beginResetModel();
    m_rows.clear();
    if (m_mgr) {
        for (const auto &s : m_mgr->conversationSummaries()) {
            Row r;
            r.peer = QString::fromStdString(s.peer);
            r.lastBody = QString::fromStdString(s.lastBody);
            r.lastDirection =
                s.lastDirection == sip::MessageDirection::Incoming
                    ? QStringLiteral("in")
                    : QStringLiteral("out");
            r.lastCreatedAtMs = s.lastCreatedAtMs;
            r.unread = s.unreadCount > 0;
            m_rows.push_back(std::move(r));
        }
    }
    endResetModel();
}

} // namespace compactphone::models

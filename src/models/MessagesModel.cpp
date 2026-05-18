#include "MessagesModel.h"

#include "core/MessagesManager.h"

namespace compactphone::models {

MessagesModel::MessagesModel(sip::MessagesManager *mgr, QObject *parent)
    : QAbstractListModel(parent), m_mgr(mgr)
{
    if (m_mgr) {
        connect(m_mgr, &sip::MessagesManager::messagesChanged,
                this, &MessagesModel::refresh);
    }
}

int MessagesModel::rowCount(const QModelIndex &parent) const
{
    if (parent.isValid()) return 0;
    return static_cast<int>(m_snapshot.size());
}

QVariant MessagesModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() ||
        index.row() < 0 ||
        index.row() >= static_cast<int>(m_snapshot.size())) {
        return {};
    }
    const auto &m = m_snapshot[index.row()];
    switch (role) {
    case IdRole:          return static_cast<qint64>(m.id);
    case AccountIdRole:   return m.accountId;
    case DirectionRole:
        return m.direction == sip::MessageDirection::Incoming
                   ? QStringLiteral("in")
                   : QStringLiteral("out");
    case BodyRole:        return QString::fromStdString(m.body);
    case CreatedAtMsRole: return static_cast<qint64>(m.createdAtMs);
    case ReadRole:        return m.read;
    }
    return {};
}

QHash<int, QByteArray> MessagesModel::roleNames() const
{
    return {
        {IdRole, "messageId"},
        {AccountIdRole, "accountId"},
        {DirectionRole, "direction"},
        {BodyRole, "body"},
        {CreatedAtMsRole, "createdAtMs"},
        {ReadRole, "read"},
    };
}

void MessagesModel::setPeer(const QString &peer)
{
    if (m_peer == peer) return;
    m_peer = peer;
    emit peerChanged();
    refresh();
}

void MessagesModel::refresh()
{
    beginResetModel();
    m_snapshot = (m_mgr && !m_peer.isEmpty())
        ? m_mgr->listByPeer(m_peer.toStdString())
        : std::vector<sip::Message>{};
    endResetModel();
}

} // namespace compactphone::models

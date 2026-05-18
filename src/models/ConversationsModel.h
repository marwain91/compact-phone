#pragma once

#include "core/Message.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class MessagesManager; }

namespace compactphone::models {

// One row per distinct peer URI, ordered by most-recent message first.
// Each row carries the last message snippet + an unread flag.
class ConversationsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        PeerRole = Qt::UserRole + 1,
        LastBodyRole,
        LastDirectionRole,
        LastCreatedAtMsRole,
        UnreadRole,
    };

    explicit ConversationsModel(sip::MessagesManager *mgr,
                                QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    struct Row {
        QString peer;
        QString lastBody;
        QString lastDirection;
        qint64 lastCreatedAtMs = 0;
        bool unread = false;
    };
    sip::MessagesManager *m_mgr;
    std::vector<Row> m_rows;
};

} // namespace compactphone::models

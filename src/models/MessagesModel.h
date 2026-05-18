#pragma once

#include "core/Message.h"

#include <QAbstractListModel>
#include <QHash>
#include <QString>

#include <vector>

namespace compactphone::sip { class MessagesManager; }

namespace compactphone::models {

// Flat list of messages exchanged with the currently-selected peer URI.
// Refreshed via refresh() when the underlying table changes; the QML
// pane sets the peer via setPeer().
class MessagesModel : public QAbstractListModel {
    Q_OBJECT
    Q_PROPERTY(QString peer READ peer WRITE setPeer NOTIFY peerChanged)
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AccountIdRole,
        DirectionRole,    // "in" / "out"
        BodyRole,
        CreatedAtMsRole,
        ReadRole,
    };

    explicit MessagesModel(sip::MessagesManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QString peer() const { return m_peer; }
    Q_INVOKABLE void setPeer(const QString &peer);
    Q_INVOKABLE void refresh();

signals:
    void peerChanged();

private:
    sip::MessagesManager *m_mgr;
    QString m_peer;
    std::vector<sip::Message> m_snapshot;
};

} // namespace compactphone::models

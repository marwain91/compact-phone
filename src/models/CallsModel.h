#pragma once

#include "core/CallEntry.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class CallManager; }

namespace compactphone::models {

class CallsModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AccountIdRole,
        RemoteUriRole,
        RemoteDisplayNameRole,
        StateRole,
        HeldRole,
        MutedRole,
        RecordingRole,
        DirectionRole,
    };

    explicit CallsModel(sip::CallManager *cm, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    sip::CallManager *m_cm;
    std::vector<sip::CallEntry> m_snapshot;
};

} // namespace compactphone::models

#pragma once

#include "core/HistoryEntry.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class HistoryManager; }

namespace compactphone::models {

class HistoryModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AccountIdRole,
        DirectionRole,
        RemoteUriRole,
        RemoteDisplayNameRole,
        StartedAtRole,
        DurationMsRole,
        EndReasonRole,
    };

    explicit HistoryModel(sip::HistoryManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    sip::HistoryManager *m_mgr;
    std::vector<sip::HistoryEntry> m_snapshot;
};

} // namespace compactphone::models

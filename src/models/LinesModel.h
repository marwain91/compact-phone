#pragma once

#include "core/WatchedLine.h"

#include <QAbstractListModel>
#include <QHash>

#include <vector>

namespace compactphone::sip { class LinesManager; }

namespace compactphone::models {

class LinesModel : public QAbstractListModel {
    Q_OBJECT
public:
    enum Roles {
        IdRole = Qt::UserRole + 1,
        AccountIdRole,
        UriRole,
        LabelRole,
        StateRole,
    };

    explicit LinesModel(sip::LinesManager *mgr, QObject *parent = nullptr);

    int rowCount(const QModelIndex &parent = {}) const override;
    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    sip::LinesManager *m_mgr;
    std::vector<sip::WatchedLine> m_snapshot;
};

} // namespace compactphone::models

#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <functional>

namespace compactphone::models { class LinesModel; }
namespace compactphone::sip { class LinesManager; }

namespace compactphone {

// Watched lines (BLF / presence) surface: CRUD + dial. Exposed to QML as
// PhoneController.lines. Non-owning pointers; the composition root owns the
// manager/model. add() needs the active account (ActiveAccountProvider
// callback); dial() places a real call, so — unlike ContactsController, which
// only pre-fills the dialer — it emits callRequested and the root dials.
class LinesController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *model READ model CONSTANT)
public:
    using ActiveAccountProvider = std::function<int()>;

    LinesController(sip::LinesManager *lines, models::LinesModel *model,
                    ActiveAccountProvider activeAccount,
                    QObject *parent = nullptr);
    ~LinesController() override;

    QAbstractListModel *model() const;

    Q_INVOKABLE int add(const QString &uri, const QString &label);
    Q_INVOKABLE bool remove(int lineId);
    Q_INVOKABLE void dial(int lineId);

signals:
    // Emitted by dial() — the composition root places the call (this is a
    // trusted in-app action, not an external URI).
    void callRequested(const QString &uri);

private:
    sip::LinesManager *m_lines = nullptr;
    models::LinesModel *m_model = nullptr;
    ActiveAccountProvider m_activeAccount;
};

} // namespace compactphone

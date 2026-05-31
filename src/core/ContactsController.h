#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>

namespace compactphone::models { class ContactsModel; }
namespace compactphone::sip { class ContactsManager; }

namespace compactphone {

// Contacts CRUD + import, owning the contacts QML surface
// (PhoneController.contacts.*). Holds non-owning pointers to the
// ContactsManager (persistence) and ContactsModel (view); both are owned by
// the composition root, mirroring AccountsController. dial() does not touch
// the dialer directly — it emits dialRequested so the root populates it.
class ContactsController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *model READ model CONSTANT)
public:
    ContactsController(sip::ContactsManager *contacts,
                       models::ContactsModel *model,
                       QObject *parent = nullptr);
    ~ContactsController() override;

    QAbstractListModel *model() const;

    Q_INVOKABLE int add(const QString &displayName, const QString &sipUri,
                        const QString &phone);
    Q_INVOKABLE bool update(int contactId, const QString &displayName,
                            const QString &sipUri, const QString &phone);
    Q_INVOKABLE bool remove(int contactId);
    Q_INVOKABLE bool setFavorite(int contactId, bool favorite);
    Q_INVOKABLE int importFromFile(const QString &path);
    Q_INVOKABLE void dial(int contactId);

signals:
    // Emitted by dial(); the composition root pre-fills the dialer with the
    // contact's URI (it does not place a call).
    void dialRequested(const QString &uri);

private:
    sip::ContactsManager *m_contacts = nullptr;
    models::ContactsModel *m_model = nullptr;
};

} // namespace compactphone

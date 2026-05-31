#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <functional>

namespace compactphone::models {
class MessagesModel;
class ConversationsModel;
}
namespace compactphone::sip {
class AccountsManager;
class MessagesManager;
}

namespace compactphone {

// SIP MESSAGE (IM) surface: the conversation list, the selected-peer thread,
// sending, and read tracking. Exposed to QML as PhoneController.messaging.
// Holds non-owning pointers (the composition root owns the managers/models).
// Sending needs the active account and a way to surface a failure notice, both
// supplied as callbacks (mirrors CallsController) so this stays account- and
// UI-agnostic.
class MessagesController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QAbstractListModel *conversations READ conversationsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *messages READ messagesModel CONSTANT)
    Q_PROPERTY(int unreadCount READ unreadCount NOTIFY unreadCountChanged)
public:
    using ActiveAccountProvider = std::function<int()>;
    using NoticeSink = std::function<void(const QString &)>;

    MessagesController(sip::AccountsManager *accounts,
                       sip::MessagesManager *messages,
                       models::MessagesModel *messagesModel,
                       models::ConversationsModel *conversationsModel,
                       ActiveAccountProvider activeAccount,
                       NoticeSink noticeSink,
                       QObject *parent = nullptr);
    ~MessagesController() override;

    QAbstractListModel *conversationsModel() const;
    QAbstractListModel *messagesModel() const;
    int unreadCount() const;

    Q_INVOKABLE bool send(const QString &peerUri, const QString &body);
    Q_INVOKABLE void selectConversation(const QString &peerUri);
    Q_INVOKABLE void markConversationRead(const QString &peerUri);

signals:
    void unreadCountChanged();

private:
    sip::AccountsManager *m_accounts = nullptr;
    sip::MessagesManager *m_messages = nullptr;
    models::MessagesModel *m_messagesModel = nullptr;
    models::ConversationsModel *m_conversationsModel = nullptr;
    ActiveAccountProvider m_activeAccount;
    NoticeSink m_noticeSink;
};

} // namespace compactphone

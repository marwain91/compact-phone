#include "MessagesController.h"

#include "AccountsManager.h"
#include "Message.h"
#include "MessagesManager.h"
#include "models/ConversationsModel.h"
#include "models/MessagesModel.h"

#include <QDateTime>

#include <utility>

namespace compactphone {

MessagesController::MessagesController(sip::AccountsManager *accounts,
                                      sip::MessagesManager *messages,
                                      models::MessagesModel *messagesModel,
                                      models::ConversationsModel *conversationsModel,
                                      ActiveAccountProvider activeAccount,
                                      NoticeSink noticeSink,
                                      QObject *parent)
    : QObject(parent),
      m_accounts(accounts),
      m_messages(messages),
      m_messagesModel(messagesModel),
      m_conversationsModel(conversationsModel),
      m_activeAccount(std::move(activeAccount)),
      m_noticeSink(std::move(noticeSink))
{
    if (m_messages) {
        connect(m_messages, &sip::MessagesManager::messagesChanged,
                this, &MessagesController::unreadCountChanged);
    }
}

MessagesController::~MessagesController() = default;

QAbstractListModel *MessagesController::conversationsModel() const
{
    return m_conversationsModel;
}

QAbstractListModel *MessagesController::messagesModel() const
{
    return m_messagesModel;
}

int MessagesController::unreadCount() const
{
    return m_messages ? m_messages->unreadCount() : 0;
}

bool MessagesController::send(const QString &peerUri, const QString &body)
{
    if (!m_accounts || !m_messages) return false;
    if (peerUri.isEmpty() || body.isEmpty()) return false;
    const int aid = m_activeAccount ? m_activeAccount() : -1;
    if (aid <= 0) return false;

    // Persist first so the user sees their outgoing bubble even if the
    // network is flaky; PJSIP retransmits MESSAGE for us.
    sip::Message m;
    m.accountId = aid;
    m.peerUri = peerUri.toStdString();
    m.direction = sip::MessageDirection::Outgoing;
    m.body = body.toStdString();
    m.createdAtMs = QDateTime::currentMSecsSinceEpoch();
    m.read = true;
    m_messages->append(m);

    const bool ok = m_accounts->sendInstantMessage(
        aid, peerUri.toStdString(), body.toStdString());
    if (!ok && m_noticeSink) m_noticeSink(tr("Message failed to send"));
    return ok;
}

void MessagesController::selectConversation(const QString &peerUri)
{
    if (m_messagesModel) m_messagesModel->setPeer(peerUri);
    markConversationRead(peerUri);
}

void MessagesController::markConversationRead(const QString &peerUri)
{
    if (m_messages) m_messages->markPeerRead(peerUri.toStdString());
}

} // namespace compactphone

#pragma once

#include "Message.h"

#include <QObject>

#include <functional>
#include <vector>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

// One row per distinct peer: the most recent message plus the count of
// unread incoming messages, computed in a single query (see
// MessagesManager::conversationSummaries).
struct ConversationSummary {
    std::string peer;
    std::string lastBody;
    MessageDirection lastDirection = MessageDirection::Outgoing;
    std::int64_t lastCreatedAtMs = 0;
    int unreadCount = 0;
};

// SQLite-backed CRUD for the `messages` table (SIP MESSAGE history).
// Emits messagesChanged() whenever the underlying table is mutated so
// listening models can refresh.
class MessagesManager : public QObject {
    Q_OBJECT
public:
    explicit MessagesManager(persistence::Database *db,
                             QObject *parent = nullptr);

    // Append a new message (inserts row, fills out msg.id, emits signal).
    bool append(Message &msg);

    // All messages, newest first.
    std::vector<Message> list(int limit = 1000) const;

    // Distinct peer URIs that appear in the table, ordered by last activity.
    std::vector<std::string> peers() const;

    // Messages for a specific peer, oldest first (for thread rendering).
    std::vector<Message> listByPeer(const std::string &peer,
                                    int limit = 500) const;

    // One summary row per peer (last message + unread count), newest-active
    // first, in a single query. Replaces the peers()+listByPeer() N+1 the
    // conversation list used to do.
    std::vector<ConversationSummary> conversationSummaries() const;

    // Mark all unread incoming messages from `peer` as read.
    bool markPeerRead(const std::string &peer);

    // Count of unread incoming messages across all peers.
    int unreadCount() const;

signals:
    void messagesChanged();

private:
    persistence::Database *m_db;
};

} // namespace compactphone::sip

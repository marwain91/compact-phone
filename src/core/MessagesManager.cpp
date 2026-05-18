#include "MessagesManager.h"

#include "persistence/Database.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

namespace {

const char *toStr(MessageDirection d)
{
    return d == MessageDirection::Incoming ? "in" : "out";
}

MessageDirection fromStr(const char *s)
{
    return (s && std::string(s) == "in") ? MessageDirection::Incoming
                                         : MessageDirection::Outgoing;
}

} // namespace

MessagesManager::MessagesManager(persistence::Database *db, QObject *parent)
    : QObject(parent), m_db(db) {}

bool MessagesManager::append(Message &msg)
{
    if (!m_db || !m_db->handle()) return false;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO messages (account_id, peer_uri, direction, body, "
        "created_at_ms, read) VALUES (?, ?, ?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("MessagesManager::append prepare: {}",
                      sqlite3_errmsg(m_db->handle()));
        return false;
    }
    sqlite3_bind_int64(stmt, 1, msg.accountId);
    sqlite3_bind_text(stmt, 2, msg.peerUri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, toStr(msg.direction), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 4, msg.body.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 5, msg.createdAtMs);
    sqlite3_bind_int(stmt, 6, msg.read ? 1 : 0);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (ok) msg.id = sqlite3_last_insert_rowid(m_db->handle());
    sqlite3_finalize(stmt);
    if (ok) emit messagesChanged();
    return ok;
}

std::vector<Message> MessagesManager::list(int limit) const
{
    std::vector<Message> out;
    if (!m_db || !m_db->handle()) return out;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, account_id, peer_uri, direction, body, created_at_ms, read "
        "FROM messages ORDER BY created_at_ms DESC LIMIT ?";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id = sqlite3_column_int64(stmt, 0);
        m.accountId = sqlite3_column_int(stmt, 1);
        m.peerUri = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        m.direction = fromStr(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
        m.body = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        m.createdAtMs = sqlite3_column_int64(stmt, 5);
        m.read = sqlite3_column_int(stmt, 6) != 0;
        out.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<std::string> MessagesManager::peers() const
{
    std::vector<std::string> out;
    if (!m_db || !m_db->handle()) return out;
    sqlite3_stmt *stmt = nullptr;
    // Group by peer, ordering each group by its most recent message time
    // so the conversation list shows newest-active first.
    const char *sql =
        "SELECT peer_uri, MAX(created_at_ms) AS last "
        "FROM messages GROUP BY peer_uri ORDER BY last DESC";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.emplace_back(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 0)));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::vector<Message> MessagesManager::listByPeer(const std::string &peer,
                                                 int limit) const
{
    std::vector<Message> out;
    if (!m_db || !m_db->handle()) return out;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, account_id, peer_uri, direction, body, created_at_ms, read "
        "FROM messages WHERE peer_uri = ? ORDER BY created_at_ms ASC LIMIT ?";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return out;
    }
    sqlite3_bind_text(stmt, 1, peer.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 2, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        Message m;
        m.id = sqlite3_column_int64(stmt, 0);
        m.accountId = sqlite3_column_int(stmt, 1);
        m.peerUri = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        m.direction = fromStr(reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3)));
        m.body = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 4));
        m.createdAtMs = sqlite3_column_int64(stmt, 5);
        m.read = sqlite3_column_int(stmt, 6) != 0;
        out.push_back(std::move(m));
    }
    sqlite3_finalize(stmt);
    return out;
}

bool MessagesManager::markPeerRead(const std::string &peer)
{
    if (!m_db || !m_db->handle()) return false;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "UPDATE messages SET read = 1 WHERE peer_uri = ? AND direction = 'in' "
        "AND read = 0";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, peer.c_str(), -1, SQLITE_TRANSIENT);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    const int changed = sqlite3_changes(m_db->handle());
    sqlite3_finalize(stmt);
    if (ok && changed > 0) emit messagesChanged();
    return ok;
}

int MessagesManager::unreadCount() const
{
    if (!m_db || !m_db->handle()) return 0;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT COUNT(*) FROM messages WHERE direction = 'in' AND read = 0";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return 0;
    }
    int n = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) n = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    return n;
}

} // namespace compactphone::sip

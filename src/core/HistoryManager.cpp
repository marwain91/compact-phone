#include "HistoryManager.h"
#include "persistence/Database.h"
#include "persistence/SqliteUtil.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

namespace {

using persistence::bindText;
using persistence::readText;

HistoryEntry rowToEntry(sqlite3_stmt *stmt)
{
    HistoryEntry e;
    e.id                = sqlite3_column_int(stmt, 0);
    e.accountId         = sqlite3_column_int(stmt, 1);
    const std::string dir = readText(stmt, 2);
    e.direction         = (dir == "in") ? CallDirection::Inbound
                                        : CallDirection::Outbound;
    e.remoteUri         = readText(stmt, 3);
    e.remoteDisplayName = readText(stmt, 4);
    e.startedAt         = sqlite3_column_int64(stmt, 5);
    e.durationMs        = sqlite3_column_int64(stmt, 6);
    e.endReason         = readText(stmt, 7);
    return e;
}

} // namespace

HistoryManager::HistoryManager(persistence::Database *db) : m_db(db) {}

HistoryId HistoryManager::append(const HistoryEntry &e)
{
    if (!m_db || !m_db->handle()) return kInvalidHistoryId;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "INSERT INTO call_history (account_id, direction, remote_uri, "
            "remote_display, started_at, duration_ms, end_reason) "
            "VALUES (?,?,?,?,?,?,?)",
            -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("HistoryManager::append prepare: {}",
                      sqlite3_errmsg(m_db->handle()));
        return kInvalidHistoryId;
    }
    sqlite3_bind_int(stmt, 1, e.accountId);
    sqlite3_bind_text(stmt, 2, callDirectionToString(e.direction), -1, SQLITE_STATIC);
    bindText(stmt, 3, e.remoteUri);
    bindText(stmt, 4, e.remoteDisplayName);
    sqlite3_bind_int64(stmt, 5, e.startedAt);
    sqlite3_bind_int64(stmt, 6, e.durationMs);
    bindText(stmt, 7, e.endReason);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    HistoryId id = ok ? static_cast<HistoryId>(sqlite3_last_insert_rowid(m_db->handle()))
                      : kInvalidHistoryId;
    sqlite3_finalize(stmt);
    if (ok) prune(e.startedAt);
    return id;
}

void HistoryManager::prune(std::int64_t referenceNowMs)
{
    if (!m_db || !m_db->handle()) return;

    // Age-based: drop entries older than the retention window. Guard against a
    // tiny reference timestamp wiping the whole table (e.g. unset clocks).
    if (referenceNowMs > kMaxAgeMs) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db->handle(),
                "DELETE FROM call_history WHERE started_at < ?",
                -1, &stmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_int64(stmt, 1, referenceNowMs - kMaxAgeMs);
            sqlite3_step(stmt);
        }
        sqlite3_finalize(stmt);
    }

    // Count-based: keep only the newest kMaxEntries rows. id DESC breaks
    // started_at ties deterministically.
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "DELETE FROM call_history WHERE id NOT IN ("
            "SELECT id FROM call_history ORDER BY started_at DESC, id DESC LIMIT ?)",
            -1, &stmt, nullptr) == SQLITE_OK) {
        sqlite3_bind_int(stmt, 1, kMaxEntries);
        sqlite3_step(stmt);
    }
    sqlite3_finalize(stmt);
}

std::vector<HistoryEntry> HistoryManager::list(int limit) const
{
    std::vector<HistoryEntry> out;
    if (!m_db || !m_db->handle()) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT id, account_id, direction, remote_uri, remote_display, "
            "started_at, duration_ms, end_reason FROM call_history "
            "ORDER BY started_at DESC LIMIT ?",
            -1, &stmt, nullptr) != SQLITE_OK) return out;
    sqlite3_bind_int(stmt, 1, limit);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(rowToEntry(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<HistoryEntry> HistoryManager::findById(HistoryId id) const
{
    if (!m_db || !m_db->handle()) return std::nullopt;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT id, account_id, direction, remote_uri, remote_display, "
            "started_at, duration_ms, end_reason FROM call_history WHERE id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, id);
    std::optional<HistoryEntry> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToEntry(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace compactphone::sip

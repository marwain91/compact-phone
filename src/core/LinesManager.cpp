#include "LinesManager.h"

#include "AccountsManager.h"
#include "persistence/Database.h"
#include "sipbackend/ISipBackend.h"

#include <sqlite3.h>

#include <algorithm>

namespace compactphone::sip {

LinesManager::LinesManager(persistence::Database *db, AccountsManager *am,
                           sipbackend::ISipBackend *backend, QObject *parent)
    : QObject(parent), m_db(db), m_am(am), m_backend(backend)
{
    loadFromDatabase();
    if (m_am) {
        // Presence arrives queued on the main thread (the backend posts
        // onPresence via EventDispatch; AccountsManager re-emits it as
        // presenceChanged). Direct connection — same thread, no metatype.
        connect(m_am, &AccountsManager::presenceChanged,
                this, &LinesManager::onPresence);
    }
    subscribeAll();
}

LinesManager::~LinesManager()
{
    // Cancel each presence watch. The adapter also tears buddies down on
    // stop()/teardown, but unwatching here is the clean path and matches the
    // old subscribePresence(false) on destruction.
    if (m_backend) {
        for (auto &e : m_entries) {
            if (e->watchId != sipbackend::kInvalidWatchId)
                m_backend->unwatch(e->watchId);
        }
    }
}

void LinesManager::loadFromDatabase()
{
    if (!m_db || !m_db->handle()) return;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, account_id, uri, label, sort_order FROM watched_lines "
        "ORDER BY sort_order, id";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto e = std::make_unique<Entry>();
        e->line.id = sqlite3_column_int(stmt, 0);
        e->line.accountId = sqlite3_column_int(stmt, 1);
        const char *u = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 2));
        const char *l = reinterpret_cast<const char *>(sqlite3_column_text(stmt, 3));
        e->line.uri = u ? u : "";
        e->line.label = l ? l : "";
        e->line.sortOrder = sqlite3_column_int(stmt, 4);
        e->line.state = LineState::Unknown;
        m_entries.push_back(std::move(e));
    }
    sqlite3_finalize(stmt);
}

void LinesManager::subscribeAll()
{
    for (auto &e : m_entries) watchEntry(*e);
}

void LinesManager::watchEntry(Entry &e)
{
    if (!m_am || !m_backend) return;
    // The backend keys watches by its own AccountId; translate from the
    // domain id. kInvalidAccountId means the account isn't registered yet —
    // skip (best-effort: presence resolves the next time lines are loaded
    // with the account registered).
    const sipbackend::AccountId backendAcc =
        m_am->backendIdFor(e.line.accountId);
    if (backendAcc == sipbackend::kInvalidAccountId) return;
    const auto wid = m_backend->watch(backendAcc, e.line.uri);
    if (wid != sipbackend::kInvalidWatchId) e.watchId = wid;
}

WatchedLineId LinesManager::add(AccountId accountId, const std::string &uri,
                                 const std::string &label)
{
    if (!m_db || !m_db->handle()) return kInvalidWatchedLineId;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "INSERT INTO watched_lines (account_id, uri, label, sort_order) "
        "VALUES (?, ?, ?, ?)";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        return kInvalidWatchedLineId;
    }
    sqlite3_bind_int(stmt, 1, accountId);
    sqlite3_bind_text(stmt, 2, uri.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, label.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 4, static_cast<int>(m_entries.size()));
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    const WatchedLineId newId = ok
        ? static_cast<WatchedLineId>(sqlite3_last_insert_rowid(m_db->handle()))
        : kInvalidWatchedLineId;
    sqlite3_finalize(stmt);
    if (!ok) return kInvalidWatchedLineId;

    auto e = std::make_unique<Entry>();
    e->line.id = newId;
    e->line.accountId = accountId;
    e->line.uri = uri;
    e->line.label = label;
    e->line.sortOrder = static_cast<int>(m_entries.size());
    e->line.state = LineState::Unknown;

    watchEntry(*e);

    m_entries.push_back(std::move(e));
    emit linesChanged();
    return newId;
}

bool LinesManager::remove(WatchedLineId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [id](const std::unique_ptr<Entry> &e) { return e->line.id == id; });
    if (it == m_entries.end()) return false;

    if (m_backend && (*it)->watchId != sipbackend::kInvalidWatchId) {
        m_backend->unwatch((*it)->watchId);
    }

    if (m_db && m_db->handle()) {
        sqlite3_stmt *stmt = nullptr;
        if (sqlite3_prepare_v2(m_db->handle(),
                "DELETE FROM watched_lines WHERE id = ?", -1, &stmt,
                nullptr) == SQLITE_OK) {
            sqlite3_bind_int(stmt, 1, id);
            sqlite3_step(stmt);
            sqlite3_finalize(stmt);
        }
    }
    m_entries.erase(it);
    emit linesChanged();
    return true;
}

std::vector<WatchedLine> LinesManager::list() const
{
    std::vector<WatchedLine> out;
    out.reserve(m_entries.size());
    for (const auto &e : m_entries) out.push_back(e->line);
    return out;
}

std::optional<WatchedLine> LinesManager::find(WatchedLineId id) const
{
    for (const auto &e : m_entries) {
        if (e->line.id == id) return e->line;
    }
    return std::nullopt;
}

void LinesManager::onPresence(sipbackend::WatchId watchId,
                              sipbackend::PresenceState state)
{
    for (auto &e : m_entries) {
        if (e->watchId != watchId) continue;
        // PresenceState mirrors LineState value-for-value (static-asserted in
        // tests/unit/test_sipbackend_types.cpp).
        const auto ls = static_cast<LineState>(state);
        if (e->line.state == ls) return;
        e->line.state = ls;
        emit linesChanged();
        return;
    }
}

} // namespace compactphone::sip

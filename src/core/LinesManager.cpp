#include "LinesManager.h"

#include "AccountsManager.h"
#include "persistence/Database.h"

#include <pjsua2.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <algorithm>

namespace compactphone::sip {

namespace {

class BuddyImpl : public pj::Buddy {
public:
    LinesManager *owner = nullptr;

    void onBuddyState() override
    {
        if (!owner) return;
        try {
            auto info = getInfo();
            LineState s = LineState::Unknown;
            if (info.subState == PJSIP_EVSUB_STATE_TERMINATED) {
                s = LineState::Offline;
            } else if (info.presStatus.status == PJSUA_BUDDY_STATUS_ONLINE) {
                // PJSIP doesn't natively map "ringing"/"on-call" to a
                // separate status; the activity field is set on busy.
                if (!info.presStatus.activity ||
                    info.presStatus.activity == PJRPID_ACTIVITY_UNKNOWN) {
                    s = LineState::Idle;
                } else {
                    s = LineState::Busy;
                }
            } else if (info.presStatus.status == PJSUA_BUDDY_STATUS_OFFLINE) {
                s = LineState::Offline;
            }
            owner->onBuddyStateChanged(this, s);
        } catch (const pj::Error &e) {
            spdlog::warn("BuddyImpl::onBuddyState: {}", e.info());
        }
    }
};

} // namespace

struct LinesManager::Entry {
    WatchedLine line;
    std::unique_ptr<BuddyImpl> buddy;
};

LinesManager::LinesManager(persistence::Database *db, AccountsManager *am,
                           QObject *parent)
    : QObject(parent), m_db(db), m_am(am)
{
    loadFromDatabase();
    subscribeAll();
}

LinesManager::~LinesManager()
{
    // Buddies must be destroyed before the PJSIP endpoint shuts down.
    for (auto &e : m_entries) {
        if (e->buddy) {
            try { e->buddy->subscribePresence(false); } catch (...) {}
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
    if (!m_am) return;
    for (auto &e : m_entries) {
        auto *acc = m_am->pjAccountFor(e->line.accountId);
        if (!acc) continue;
        try {
            auto buddy = std::make_unique<BuddyImpl>();
            buddy->owner = this;
            pj::BuddyConfig bc;
            bc.uri = e->line.uri;
            bc.subscribe = true;
            buddy->create(*acc, bc);
            buddy->subscribePresence(true);
            e->buddy = std::move(buddy);
        } catch (const pj::Error &err) {
            spdlog::error("LinesManager: buddy create failed for {}: {}",
                          e->line.uri, err.info());
        }
    }
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

    // Subscribe immediately.
    if (m_am) {
        auto *acc = m_am->pjAccountFor(accountId);
        if (acc) {
            try {
                auto buddy = std::make_unique<BuddyImpl>();
                buddy->owner = this;
                pj::BuddyConfig bc;
                bc.uri = uri;
                bc.subscribe = true;
                buddy->create(*acc, bc);
                buddy->subscribePresence(true);
                e->buddy = std::move(buddy);
            } catch (const pj::Error &err) {
                spdlog::error("LinesManager::add buddy create: {}", err.info());
            }
        }
    }

    m_entries.push_back(std::move(e));
    emit linesChanged();
    return newId;
}

bool LinesManager::remove(WatchedLineId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
        [id](const std::unique_ptr<Entry> &e) { return e->line.id == id; });
    if (it == m_entries.end()) return false;

    if ((*it)->buddy) {
        try { (*it)->buddy->subscribePresence(false); } catch (...) {}
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

LinesManager::Entry *LinesManager::findByBuddy(pj::Buddy *buddy)
{
    for (auto &e : m_entries) {
        if (e->buddy.get() == buddy) return e.get();
    }
    return nullptr;
}

LinesManager::Entry *LinesManager::findById(WatchedLineId id)
{
    for (auto &e : m_entries) {
        if (e->line.id == id) return e.get();
    }
    return nullptr;
}

void LinesManager::onBuddyStateChanged(pj::Buddy *buddy, LineState state)
{
    auto *e = findByBuddy(buddy);
    if (!e) return;
    if (e->line.state == state) return;
    e->line.state = state;
    emit linesChanged();
}

} // namespace compactphone::sip

#include "ContactsManager.h"
#include "persistence/Database.h"
#include "persistence/SqliteUtil.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

namespace {

using persistence::bindText;
using persistence::readText;

Contact rowToContact(sqlite3_stmt *stmt)
{
    Contact c;
    c.id          = sqlite3_column_int(stmt, 0);
    c.displayName = readText(stmt, 1);
    c.sipUri      = readText(stmt, 2);
    c.phone       = readText(stmt, 3);
    c.notes       = readText(stmt, 4);
    c.favorite    = sqlite3_column_int(stmt, 5) != 0;
    return c;
}

} // namespace

ContactsManager::ContactsManager(persistence::Database *db) : m_db(db) {}

ContactId ContactsManager::add(const Contact &c)
{
    if (!m_db || !m_db->handle()) return kInvalidContactId;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "INSERT INTO contacts (display_name, sip_uri, phone, notes, favorite) "
            "VALUES (?,?,?,?,?)",
            -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("ContactsManager::add prepare: {}",
                      sqlite3_errmsg(m_db->handle()));
        return kInvalidContactId;
    }
    bindText(stmt, 1, c.displayName);
    bindText(stmt, 2, c.sipUri);
    bindText(stmt, 3, c.phone);
    bindText(stmt, 4, c.notes);
    sqlite3_bind_int(stmt, 5, c.favorite ? 1 : 0);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    ContactId id = ok ? static_cast<ContactId>(sqlite3_last_insert_rowid(m_db->handle()))
                      : kInvalidContactId;
    sqlite3_finalize(stmt);
    return id;
}

bool ContactsManager::update(const Contact &c)
{
    if (!m_db || !m_db->handle()) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "UPDATE contacts SET display_name=?, sip_uri=?, phone=?, "
            "notes=?, favorite=? WHERE id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    bindText(stmt, 1, c.displayName);
    bindText(stmt, 2, c.sipUri);
    bindText(stmt, 3, c.phone);
    bindText(stmt, 4, c.notes);
    sqlite3_bind_int(stmt, 5, c.favorite ? 1 : 0);
    sqlite3_bind_int(stmt, 6, c.id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE
                    && sqlite3_changes(m_db->handle()) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

bool ContactsManager::remove(ContactId id)
{
    if (!m_db || !m_db->handle()) return false;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "DELETE FROM contacts WHERE id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE
                    && sqlite3_changes(m_db->handle()) > 0;
    sqlite3_finalize(stmt);
    return ok;
}

std::vector<Contact> ContactsManager::list() const
{
    std::vector<Contact> out;
    if (!m_db || !m_db->handle()) return out;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT id, display_name, sip_uri, phone, notes, favorite "
            "FROM contacts ORDER BY display_name",
            -1, &stmt, nullptr) != SQLITE_OK) return out;
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        out.push_back(rowToContact(stmt));
    }
    sqlite3_finalize(stmt);
    return out;
}

std::optional<Contact> ContactsManager::findById(ContactId id) const
{
    if (!m_db || !m_db->handle()) return std::nullopt;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT id, display_name, sip_uri, phone, notes, favorite "
            "FROM contacts WHERE id=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    sqlite3_bind_int(stmt, 1, id);
    std::optional<Contact> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToContact(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

std::optional<Contact> ContactsManager::findByUri(const std::string &sipUri) const
{
    if (!m_db || !m_db->handle()) return std::nullopt;
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "SELECT id, display_name, sip_uri, phone, notes, favorite "
            "FROM contacts WHERE sip_uri=?",
            -1, &stmt, nullptr) != SQLITE_OK) return std::nullopt;
    bindText(stmt, 1, sipUri);
    std::optional<Contact> result;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        result = rowToContact(stmt);
    }
    sqlite3_finalize(stmt);
    return result;
}

} // namespace compactphone::sip

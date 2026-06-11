#include "AccountsManager.h"
#include "persistence/Database.h"
#include "persistence/SqliteUtil.h"
#include "platform/Keychain.h"
#include "sipbackend/pjsip/PjsipBackend.h"

#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <QUuid>

#include <algorithm>
#include <mutex>

namespace compactphone::sip {

namespace {

std::string newPasswordRef()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

} // namespace

using persistence::bindText;
using persistence::readText;

// ---------------------------------------------------------------------------
// mapRegEvent — manager-side policy, unit-testable without a live backend
// ---------------------------------------------------------------------------

RegStateUpdate mapRegEvent(bool regIsActive, int statusCode,
                           const std::string &reason,
                           const RegError &lastError)
{
    // Branch order matters: an active registration with a 2xx wins, then a
    // code of 0 means PJSIP is reporting progress (REGISTER sent, no final
    // response yet) regardless of the active flag, then a 2xx on an
    // inactive registration is a confirmed unregister. Everything else —
    // including a non-2xx final response while the old binding is still
    // nominally active (e.g. a 401/403 on refresh) — is a failure.
    RegStateUpdate upd;
    if (regIsActive && statusCode / 100 == 2) {
        upd.state = RegistrationState::Registered;
        upd.error = {}; // cleared on success
    } else if (statusCode == 0) {
        upd.state = RegistrationState::Registering;
        upd.error = lastError; // preserved while the attempt is in flight
    } else if (!regIsActive && statusCode / 100 == 2) {
        upd.state = RegistrationState::Unregistered;
        upd.error = lastError; // preserved so the reason stays readable
    } else {
        upd.state = RegistrationState::Failed;
        upd.error.code = statusCode;
        upd.error.reason = reason;
    }
    return upd;
}

// ---------------------------------------------------------------------------
// Entry — one DB row + optional live backend id
// ---------------------------------------------------------------------------

struct AccountsManager::Entry {
    Account account;
    // true iff this account has a live entry in m_backendIds (i.e. it has
    // been added to the backend and not yet removed).  Invariant:
    //   registered  ⟺  m_backendIds contains account.id
    bool registered = false;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

AccountsManager::AccountsManager(sipbackend::ISipBackend *backend,
                                 persistence::Database *db,
                                 platform::IKeychain *keychain)
    : m_backend(backend), m_db(db), m_keychain(keychain)
{
    loadFromDatabase();
    // NOTE: startup accounts are NOT registered here. The caller must
    // call backend->setListener(this) first so queued reg-state events
    // have a destination, then call registerStartupAccounts(). Both
    // buildCoreSipGraph and SipManagerPair do this in the correct order.
}

AccountsManager::~AccountsManager()
{
    // Unregister all live accounts from the backend. Main-thread-only — no
    // lock needed since phase 3.
    std::vector<sipbackend::AccountId> toRemove;
    toRemove.reserve(m_backendIds.size());
    for (const auto &kv : m_backendIds) {
        toRemove.push_back(kv.second);
    }
    for (const auto backendId : toRemove) {
        try { m_backend->removeAccount(backendId); } catch (...) {}
    }
}

// ---------------------------------------------------------------------------
// Database
// ---------------------------------------------------------------------------

void AccountsManager::loadFromDatabase()
{
    if (!m_db || !m_db->handle()) return;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, display_name, username, domain, auth_user, "
        "auth_realm, password_ref, transport, proxy, stun_server, "
        "register_on_startup, srtp_mode, allow_untrusted_cert, "
        "dtmf_method, is_default, enabled, sort_order, label, "
        "public_address, codecs, voicemail_number, "
        "register_interval_sec, keepalive_interval_sec, "
        "session_timers_enabled, publish_presence_enabled, "
        "ice_enabled, hide_caller_id, zrtp_enabled, provider "
        "FROM accounts ORDER BY sort_order, id";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("loadFromDatabase prepare failed: {}",
                      sqlite3_errmsg(m_db->handle()));
        return;
    }
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto entry = std::make_unique<Entry>();
        auto &a = entry->account;
        a.id              = sqlite3_column_int(stmt, 0);
        a.displayName     = readText(stmt, 1);
        a.username        = readText(stmt, 2);
        a.domain          = readText(stmt, 3);
        a.authUser        = readText(stmt, 4);
        a.authRealm       = readText(stmt, 5);
        a.passwordRef     = readText(stmt, 6);
        a.transport       = transportFromString(readText(stmt, 7));
        a.proxy           = readText(stmt, 8);
        a.stunServer      = readText(stmt, 9);
        a.registerOnStartup = sqlite3_column_int(stmt, 10) != 0;
        a.srtpMode        = srtpModeFromString(readText(stmt, 11));
        a.allowUntrustedCert = sqlite3_column_int(stmt, 12) != 0;
        a.dtmfMethod      = dtmfMethodFromString(readText(stmt, 13));
        a.isDefault       = sqlite3_column_int(stmt, 14) != 0;
        a.enabled         = sqlite3_column_int(stmt, 15) != 0;
        a.sortOrder       = sqlite3_column_int(stmt, 16);
        a.label           = readText(stmt, 17);
        if (a.label.empty()) a.label = a.displayName;
        a.publicAddress         = readText(stmt, 18);
        a.codecs                = readText(stmt, 19);
        a.voicemailNumber       = readText(stmt, 20);
        a.registerIntervalSec   = sqlite3_column_int(stmt, 21);
        a.keepaliveIntervalSec  = sqlite3_column_int(stmt, 22);
        a.sessionTimersEnabled  = sqlite3_column_int(stmt, 23) != 0;
        a.publishPresenceEnabled = sqlite3_column_int(stmt, 24) != 0;
        a.iceEnabled            = sqlite3_column_int(stmt, 25) != 0;
        a.hideCallerId          = sqlite3_column_int(stmt, 26) != 0;
        a.zrtpEnabled           = sqlite3_column_int(stmt, 27) != 0;
        a.provider              = readText(stmt, 28);
        m_entries.push_back(std::move(entry));
    }
    sqlite3_finalize(stmt);
}

AccountId AccountsManager::add(const Account &acc, const std::string &password)
{
    Account a = acc;
    a.passwordRef = newPasswordRef();
    if (!m_keychain->set(a.passwordRef, password)) {
        spdlog::error("AccountsManager::add: keychain set failed");
        return kInvalidAccountId;
    }
    if (!insertRow(a)) {
        m_keychain->erase(a.passwordRef);
        return kInvalidAccountId;
    }
    auto entry = std::make_unique<Entry>();
    entry->account = a;
    m_entries.push_back(std::move(entry));
    // Seed the cache so the first registerAccount below doesn't go through
    // the OS keychain (avoids an immediate ACL prompt right after add).
    m_passwordCache[a.passwordRef] = password;
    if (a.enabled && a.registerOnStartup) registerAccount(a.id);
    return a.id;
}

bool AccountsManager::insertRow(Account &acc)
{
    const char *sql =
        "INSERT INTO accounts (label, display_name, username, domain, auth_user, "
        "auth_realm, password_ref, transport, proxy, stun_server, public_address, codecs, "
        "voicemail_number, register_on_startup, register_interval_sec, "
        "keepalive_interval_sec, session_timers_enabled, publish_presence_enabled, "
        "ice_enabled, hide_caller_id, zrtp_enabled, srtp_mode, allow_untrusted_cert, "
        "dtmf_method, is_default, enabled, sort_order, provider) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("insertRow prepare: {}", sqlite3_errmsg(m_db->handle()));
        return false;
    }
    bindText(stmt, 1, acc.label);
    bindText(stmt, 2, acc.displayName);
    bindText(stmt, 3, acc.username);
    bindText(stmt, 4, acc.domain);
    bindText(stmt, 5, acc.authUser);
    bindText(stmt, 6, acc.authRealm);
    bindText(stmt, 7, acc.passwordRef);
    sqlite3_bind_text(stmt, 8, transportToString(acc.transport), -1, SQLITE_STATIC);
    bindText(stmt, 9, acc.proxy);
    bindText(stmt, 10, acc.stunServer);
    bindText(stmt, 11, acc.publicAddress);
    bindText(stmt, 12, acc.codecs);
    bindText(stmt, 13, acc.voicemailNumber);
    sqlite3_bind_int(stmt, 14, acc.registerOnStartup ? 1 : 0);
    sqlite3_bind_int(stmt, 15, acc.registerIntervalSec);
    sqlite3_bind_int(stmt, 16, acc.keepaliveIntervalSec);
    sqlite3_bind_int(stmt, 17, acc.sessionTimersEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 18, acc.publishPresenceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 19, acc.iceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 20, acc.hideCallerId ? 1 : 0);
    sqlite3_bind_int(stmt, 21, acc.zrtpEnabled ? 1 : 0);
    sqlite3_bind_text(stmt, 22, srtpModeToString(acc.srtpMode), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 23, acc.allowUntrustedCert ? 1 : 0);
    sqlite3_bind_text(stmt, 24, dtmfMethodToString(acc.dtmfMethod), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 25, acc.isDefault ? 1 : 0);
    sqlite3_bind_int(stmt, 26, acc.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 27, acc.sortOrder);
    bindText(stmt, 28, acc.provider);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    if (!ok) {
        spdlog::error("insertRow step: {}", sqlite3_errmsg(m_db->handle()));
    }
    if (ok) {
        acc.id = static_cast<AccountId>(sqlite3_last_insert_rowid(m_db->handle()));
    }
    sqlite3_finalize(stmt);
    return ok;
}

bool AccountsManager::remove(AccountId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;
    if ((*it)->registered) {
        unregisterAccount(id);
    }
    m_keychain->erase((*it)->account.passwordRef);
    m_passwordCache.erase((*it)->account.passwordRef);
    if (!deleteRow(id)) return false;
    m_entries.erase(it);
    return true;
}

bool AccountsManager::deleteRow(AccountId id)
{
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
                           "DELETE FROM accounts WHERE id = ?",
                           -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_int(stmt, 1, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    return ok;
}

bool AccountsManager::update(const Account &acc)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [&acc](const auto &e) { return e->account.id == acc.id; });
    if (it == m_entries.end()) return false;

    const char *sql =
        "UPDATE accounts SET "
        "label = ?, display_name = ?, username = ?, domain = ?, auth_user = ?, "
        "auth_realm = ?, transport = ?, proxy = ?, stun_server = ?, public_address = ?, "
        "codecs = ?, voicemail_number = ?, register_on_startup = ?, "
        "register_interval_sec = ?, keepalive_interval_sec = ?, "
        "session_timers_enabled = ?, publish_presence_enabled = ?, "
        "ice_enabled = ?, hide_caller_id = ?, zrtp_enabled = ?, "
        "srtp_mode = ?, allow_untrusted_cert = ?, dtmf_method = ?, "
        "enabled = ?, sort_order = ?, provider = ? WHERE id = ?";
    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("update prepare: {}", sqlite3_errmsg(m_db->handle()));
        return false;
    }
    bindText(stmt, 1, acc.label);
    bindText(stmt, 2, acc.displayName);
    bindText(stmt, 3, acc.username);
    bindText(stmt, 4, acc.domain);
    bindText(stmt, 5, acc.authUser);
    bindText(stmt, 6, acc.authRealm);
    sqlite3_bind_text(stmt, 7, transportToString(acc.transport), -1, SQLITE_STATIC);
    bindText(stmt, 8, acc.proxy);
    bindText(stmt, 9, acc.stunServer);
    bindText(stmt, 10, acc.publicAddress);
    bindText(stmt, 11, acc.codecs);
    bindText(stmt, 12, acc.voicemailNumber);
    sqlite3_bind_int(stmt, 13, acc.registerOnStartup ? 1 : 0);
    sqlite3_bind_int(stmt, 14, acc.registerIntervalSec);
    sqlite3_bind_int(stmt, 15, acc.keepaliveIntervalSec);
    sqlite3_bind_int(stmt, 16, acc.sessionTimersEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 17, acc.publishPresenceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 18, acc.iceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 19, acc.hideCallerId ? 1 : 0);
    sqlite3_bind_int(stmt, 20, acc.zrtpEnabled ? 1 : 0);
    sqlite3_bind_text(stmt, 21, srtpModeToString(acc.srtpMode), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 22, acc.allowUntrustedCert ? 1 : 0);
    sqlite3_bind_text(stmt, 23, dtmfMethodToString(acc.dtmfMethod), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 24, acc.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 25, acc.sortOrder);
    bindText(stmt, 26, acc.provider);
    sqlite3_bind_int(stmt, 27, acc.id);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) return false;

    const bool hadLiveAccount = (*it)->registered;
    if (hadLiveAccount) {
        unregisterAccount(acc.id);
    }

    // Preserve isDefault (managed by setDefault) and passwordRef (immutable;
    // password changes go through keychain.set() separately).
    const bool wasDefault = (*it)->account.isDefault;
    const auto pwdRef = (*it)->account.passwordRef;
    (*it)->account = acc;
    (*it)->account.isDefault = wasDefault;
    (*it)->account.passwordRef = pwdRef;

    if ((*it)->account.enabled && (hadLiveAccount || (*it)->account.registerOnStartup)) {
        registerAccount(acc.id);
    }
    return true;
}

std::vector<Account> AccountsManager::list() const
{
    std::vector<Account> out;
    out.reserve(m_entries.size());
    for (const auto &e : m_entries) out.push_back(e->account);
    return out;
}

std::optional<Account> AccountsManager::find(AccountId id) const
{
    for (const auto &e : m_entries) {
        if (e->account.id == id) return e->account;
    }
    return std::nullopt;
}

AccountId AccountsManager::defaultAccountId() const
{
    for (const auto &e : m_entries) {
        if (e->account.isDefault && e->account.enabled) return e->account.id;
    }
    for (const auto &e : m_entries) {
        if (e->account.enabled) return e->account.id;
    }
    return kInvalidAccountId;
}

bool AccountsManager::setDefault(AccountId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;

    auto run = [&](const char *sql) {
        return sqlite3_exec(m_db->handle(), sql, nullptr, nullptr, nullptr)
               == SQLITE_OK;
    };

    if (!run("BEGIN TRANSACTION")) return false;
    if (!run("UPDATE accounts SET is_default = 0 WHERE is_default = 1")) {
        run("ROLLBACK"); return false;
    }

    sqlite3_stmt *set = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "UPDATE accounts SET is_default = 1 WHERE id = ?",
            -1, &set, nullptr) != SQLITE_OK) {
        run("ROLLBACK"); return false;
    }
    sqlite3_bind_int(set, 1, id);
    const bool setOk = sqlite3_step(set) == SQLITE_DONE;
    sqlite3_finalize(set);
    if (!setOk) { run("ROLLBACK"); return false; }
    if (!run("COMMIT")) return false;

    for (auto &e : m_entries) {
        e->account.isDefault = (e->account.id == id);
    }
    return true;
}

bool AccountsManager::setEnabled(AccountId id, bool enabled)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;
    auto &e = **it;
    if (e.account.enabled == enabled) return true;

    sqlite3_stmt *stmt = nullptr;
    if (sqlite3_prepare_v2(m_db->handle(),
            "UPDATE accounts SET enabled = ? WHERE id = ?",
            -1, &stmt, nullptr) != SQLITE_OK) return false;
    sqlite3_bind_int(stmt, 1, enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 2, id);
    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) return false;

    e.account.enabled = enabled;
    if (enabled) {
        registerAccount(id);
    } else {
        unregisterAccount(id);
    }
    return true;
}

bool AccountsManager::setPassword(AccountId id, const std::string &password)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;
    auto &e = **it;
    // Keep the same passwordRef so existing in-flight backend credentials
    // and the DB row don't need to change.
    if (!m_keychain->set(e.account.passwordRef, password)) {
        spdlog::error("setPassword: keychain set failed");
        return false;
    }
    m_passwordCache[e.account.passwordRef] = password;
    // Rebuild the backend account so the new credentials take effect now.
    const bool wasLive = e.registered;
    if (wasLive) {
        unregisterAccount(id);
    }
    if (e.account.enabled && (wasLive || e.account.registerOnStartup)) {
        registerAccount(id);
    }
    return true;
}

// ---------------------------------------------------------------------------
// Registration — drives the backend via ISipBackend
// ---------------------------------------------------------------------------

void AccountsManager::registerStartupAccounts()
{
    for (auto &e : m_entries) {
        if (e->account.enabled && e->account.registerOnStartup) {
            registerAccount(e->account.id);
        }
    }
}

bool AccountsManager::registerAccount(AccountId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;
    auto &e = **it;
    if (!m_backend || !m_backend->isRunning()) return false;
    if (e.registered) return true; // already registered

    // In-memory cache short-circuits the macOS Keychain prompt on every
    // subsequent registerAccount within the same session (network back,
    // wake, account-enable toggle, etc).
    std::string password;
    if (auto cached = m_passwordCache.find(e.account.passwordRef);
        cached != m_passwordCache.end()) {
        password = cached->second;
    } else {
        auto fetched = m_keychain->get(e.account.passwordRef);
        if (!fetched) {
            spdlog::error("registerAccount: keychain missing ref {}",
                          e.account.passwordRef);
            return false;
        }
        password = *fetched;
        m_passwordCache.emplace(e.account.passwordRef, password);
    }

    // Build the stack-neutral AccountSettings from the Account value.
    // authUser/authRealm are passed RAW; the adapter defaults them
    // (authUser → username, authRealm → "*") only when empty.
    sipbackend::AccountSettings settings;
    settings.displayName           = e.account.displayName;
    settings.username              = e.account.username;
    settings.domain                = e.account.domain;
    settings.authUser              = e.account.authUser;
    settings.authRealm             = e.account.authRealm;
    settings.password              = password;
    settings.transport             = static_cast<sipbackend::Transport>(
                                         static_cast<int>(e.account.transport));
    settings.proxy                 = e.account.proxy;
    settings.publicAddress         = e.account.publicAddress;
    settings.registerIntervalSec   = e.account.registerIntervalSec;
    settings.keepaliveIntervalSec  = e.account.keepaliveIntervalSec;
    settings.sessionTimersEnabled  = e.account.sessionTimersEnabled;
    settings.publishPresenceEnabled = e.account.publishPresenceEnabled;
    settings.iceEnabled            = e.account.iceEnabled;
    settings.hideCallerId          = e.account.hideCallerId;
    settings.zrtpEnabled           = e.account.zrtpEnabled;
    settings.srtpMode              = static_cast<sipbackend::SrtpMode>(
                                         static_cast<int>(e.account.srtpMode));
    settings.allowUntrustedCert    = e.account.allowUntrustedCert;
    settings.dtmfMethod            = static_cast<sipbackend::DtmfMethod>(
                                         static_cast<int>(e.account.dtmfMethod));
    // STUN opt-in: when the account has a STUN server configured, ask the
    // backend to use the global STUN config for this account. The STUN server
    // itself is loaded into the engine at start time (SipEngine::start /
    // PhoneController — phase-4 scope to route via the backend).
    settings.useStun               = !e.account.stunServer.empty();

    const sipbackend::AccountId backendId = m_backend->addAccount(settings);
    if (backendId == sipbackend::kInvalidAccountId) {
        spdlog::error("registerAccount: backend addAccount failed for id {}", id);
        return false;
    }
    m_backendIds[id] = backendId;
    e.registered = true;
    return true;
}

void AccountsManager::unregisterAccount(AccountId id)
{
    for (auto &e : m_entries) {
        if (e->account.id != id || !e->registered) continue;

        sipbackend::AccountId backendId = sipbackend::kInvalidAccountId;
        {
            auto it = m_backendIds.find(id);
            if (it != m_backendIds.end()) {
                backendId = it->second;
                m_backendIds.erase(it);
            }
        }
        if (backendId != sipbackend::kInvalidAccountId) {
            m_backend->removeAccount(backendId);
        }
        e->registered = false;
        // Reset to Unregistered and clear the stored error so that a later
        // re-register starts with a clean slate — matching the old impl where
        // the error lived on the AccountImpl object that was destroyed on
        // unregister, making lastRegErrorOf() return RegError{} until the
        // next failure.
        m_regStates[id] = RegistrationState::Unregistered;
        m_regErrors.erase(id);
        return;
    }
}

// ---------------------------------------------------------------------------
// ISipBackendListener — events arrive QUEUED on the main thread
// ---------------------------------------------------------------------------

void AccountsManager::onRegState(sipbackend::AccountId backendId,
                                 bool regActive, int sipCode,
                                 const std::string &reason)
{
    const AccountId domainId = accountIdForBackend(backendId);
    if (domainId == kInvalidAccountId) {
        // Stale post-removal event — guaranteed to happen; ignore silently.
        spdlog::debug("AccountsManager::onRegState: unknown backend id {} "
                      "(stale post-removal event)", backendId);
        return;
    }

    const auto upd = mapRegEvent(regActive, sipCode, reason, m_regErrors[domainId]);
    m_regStates[domainId] = upd.state;
    m_regErrors[domainId] = upd.error;

    spdlog::info("AccountsManager: account {} reg state: {} code={} reason='{}'",
                 domainId,
                 upd.state == RegistrationState::Registered ? "Registered"
                 : upd.state == RegistrationState::Registering ? "Registering"
                 : upd.state == RegistrationState::Failed ? "Failed"
                 : "Unregistered",
                 sipCode, reason);

    std::lock_guard<std::mutex> lk(m_callbackMutex);
    if (m_cb) m_cb(domainId, upd.state);
}

void AccountsManager::onMwi(sipbackend::AccountId backendId,
                            int newMessages, int oldMessages, bool active)
{
    const AccountId domainId = accountIdForBackend(backendId);
    if (domainId == kInvalidAccountId) {
        spdlog::debug("AccountsManager::onMwi: unknown backend id {}", backendId);
        return;
    }
    updateMwi(domainId, newMessages, oldMessages, active);
}

void AccountsManager::onInstantMessage(sipbackend::AccountId backendId,
                                       const std::string &fromUri,
                                       const std::string &body)
{
    const AccountId domainId = accountIdForBackend(backendId);
    if (domainId == kInvalidAccountId) {
        spdlog::debug("AccountsManager::onInstantMessage: unknown backend id {}",
                      backendId);
        return;
    }
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    if (m_onInstantMessage) m_onInstantMessage(domainId, fromUri, body);
}

// ---------------------------------------------------------------------------
// MWI
// ---------------------------------------------------------------------------

MwiState AccountsManager::mwiStateOf(AccountId id) const
{
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    auto it = m_mwi.find(id);
    return it == m_mwi.end() ? MwiState{} : it->second;
}

void AccountsManager::updateMwi(AccountId id, int newCount, int oldCount,
                                bool active)
{
    // May be called from the main thread (via queued onMwi listener event).
    // The map and the callback are read on the main thread.
    MwiState s;
    s.newMessages = newCount;
    s.oldMessages = oldCount;
    s.active = active;
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    m_mwi[id] = s;
    if (m_onMwi) m_onMwi(id, s);
}

void AccountsManager::setOnMwiChanged(
    std::function<void(AccountId, MwiState)> cb)
{
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    m_onMwi = std::move(cb);
}

// ---------------------------------------------------------------------------
// Instant message
// ---------------------------------------------------------------------------

bool AccountsManager::sendInstantMessage(AccountId accountId,
                                         const std::string &to,
                                         const std::string &body)
{
    const sipbackend::AccountId backendId = backendIdFor(accountId);
    if (backendId == sipbackend::kInvalidAccountId) return false;
    return m_backend->sendMessage(backendId, to, body);
}

void AccountsManager::setOnInstantMessage(
    std::function<void(AccountId, const std::string &,
                       const std::string &)> cb)
{
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    m_onInstantMessage = std::move(cb);
}

// ---------------------------------------------------------------------------
// Reregister
// ---------------------------------------------------------------------------

void AccountsManager::reregisterAllEnabled()
{
    // Snapshot the IDs first: unregisterAccount erases from m_backendIds and
    // sets registered=false, while registerAccount inserts into m_backendIds
    // and sets registered=true — neither mutates m_entries itself, so iterator
    // invalidation is not actually the concern here. The snapshot is still the
    // right shape: it avoids relying on the loop body not mutating the
    // collection we are iterating and makes the intent explicit.
    std::vector<AccountId> targets;
    targets.reserve(m_entries.size());
    for (const auto &e : m_entries) {
        if (e->account.enabled && e->account.registerOnStartup) {
            targets.push_back(e->account.id);
        }
    }
    for (auto id : targets) {
        unregisterAccount(id);
        registerAccount(id);
    }
    spdlog::info("AccountsManager: re-registered {} account(s)",
                 targets.size());
}

// ---------------------------------------------------------------------------
// State reads
// ---------------------------------------------------------------------------

RegistrationState AccountsManager::stateOf(AccountId id) const
{
    auto it = m_regStates.find(id);
    return it == m_regStates.end() ? RegistrationState::Unregistered : it->second;
}

RegError AccountsManager::lastRegErrorOf(AccountId id) const
{
    auto it = m_regErrors.find(id);
    return it == m_regErrors.end() ? RegError{} : it->second;
}

// ---------------------------------------------------------------------------
// Callback setters
// ---------------------------------------------------------------------------

void AccountsManager::setOnRegistrationStateChanged(
    std::function<void(AccountId, RegistrationState)> cb)
{
    std::lock_guard<std::mutex> lk(m_callbackMutex);
    m_cb = std::move(cb);
}

// ---------------------------------------------------------------------------
// Backend-id translation (main-thread-only)
// ---------------------------------------------------------------------------

sipbackend::AccountId AccountsManager::backendIdFor(AccountId id) const
{
    auto it = m_backendIds.find(id);
    return it != m_backendIds.end() ? it->second
                                    : sipbackend::kInvalidAccountId;
}

AccountId AccountsManager::accountIdForBackend(
    sipbackend::AccountId backendId) const
{
    for (const auto &kv : m_backendIds) {
        if (kv.second == backendId) return kv.first;
    }
    return kInvalidAccountId;
}

// ---------------------------------------------------------------------------
// Presence bridge — pj::Account access for LinesManager (BLF). Removed in
// phase 5 when presence moves behind ISipBackend.
// ---------------------------------------------------------------------------

pj::Account *AccountsManager::pjAccountFor(AccountId id)
{
    auto *pjsip = dynamic_cast<sipbackend::PjsipBackend *>(m_backend);
    if (!pjsip) return nullptr;   // non-PJSIP backend (fake / future adapters)
    const sipbackend::AccountId backendId = backendIdFor(id);
    if (backendId == sipbackend::kInvalidAccountId) return nullptr;
    return pjsip->pjAccountFor(backendId);
}

// ---------------------------------------------------------------------------
// Test accessors
// ---------------------------------------------------------------------------

std::string AccountsManager::passwordRefFor(AccountId id) const
{
    for (const auto &e : m_entries) {
        if (e->account.id == id) return e->account.passwordRef;
    }
    return {};
}

} // namespace compactphone::sip

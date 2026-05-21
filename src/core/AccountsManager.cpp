#include "AccountsManager.h"
#include "SipEngine.h"
#include "persistence/Database.h"
#include "platform/Keychain.h"

#include <pjsua-lib/pjsua.h>
#include <pjsua2.hpp>
#include <sqlite3.h>
#include <spdlog/spdlog.h>

#include <QUuid>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace compactphone::sip {

namespace {

const char *transportScheme(Transport t)
{
    switch (t) {
    case Transport::Tcp: return ";transport=tcp";
    case Transport::Tls: return ";transport=tls";
    case Transport::Udp:
    default:
        // Explicitly state UDP so PJSIP does not size-escalate to TCP for
        // large requests (RFC 3261 §18.1.1) and so any cached non-UDP
        // connection to the registrar isn't reused. The parameter also
        // makes the chosen transport visible on the wire / in pcaps.
        return ";transport=udp";
    }
}

std::string newPasswordRef()
{
    return QUuid::createUuid().toString(QUuid::WithoutBraces).toStdString();
}

// Helper: bind a std::string by value (sqlite3 copies with SQLITE_TRANSIENT).
void bindText(sqlite3_stmt *stmt, int idx, const std::string &s)
{
    sqlite3_bind_text(stmt, idx, s.data(), static_cast<int>(s.size()),
                      SQLITE_TRANSIENT);
}

} // namespace

class AccountsManager::AccountImpl : public pj::Account {
public:
    AccountId id = kInvalidAccountId;
    AccountsManager *owner = nullptr;
    std::atomic<RegistrationState> state{RegistrationState::Unregistered};
    // Guarded by ownersMutex on read; written only from the PJSIP thread
    // inside onRegState while the same mutex is held.
    mutable std::mutex errMutex;
    RegError lastError;

    void onRegState(pj::OnRegStateParam &prm) override
    {
        pj::AccountInfo info = getInfo();
        spdlog::info("Account {} reg state: active={} code={} reason='{}'",
                     id, info.regIsActive, static_cast<int>(prm.code),
                     prm.reason);
        RegistrationState s;
        if (info.regIsActive && prm.code / 100 == 2) {
            s = RegistrationState::Registered;
        } else if (prm.code == 0) {
            s = RegistrationState::Registering;
        } else if (!info.regIsActive && prm.code / 100 == 2) {
            s = RegistrationState::Unregistered;
        } else {
            s = RegistrationState::Failed;
        }
        {
            std::lock_guard<std::mutex> lk(errMutex);
            if (s == RegistrationState::Failed) {
                lastError.code = static_cast<int>(prm.code);
                lastError.reason = prm.reason;
            } else if (s == RegistrationState::Registered) {
                lastError = {};
            }
            // Registering / Unregistered preserve the previous error so the
            // user can still read the reason while a retry is in flight.
        }
        state.store(s);
        if (owner && owner->m_cb) owner->m_cb(id, s);
    }

    void onIncomingCall(pj::OnIncomingCallParam &prm) override
    {
        spdlog::info("Account {} incoming call (pjsip id {})", id, prm.callId);
        if (owner && owner->m_onIncoming) owner->m_onIncoming(id, prm.callId);
    }

    // Inbound SIP MESSAGE (RFC 3428 instant message).
    void onInstantMessage(pj::OnInstantMessageParam &prm) override
    {
        if (owner && owner->m_onInstantMessage) {
            owner->m_onInstantMessage(id, prm.fromUri, prm.msgBody);
        }
    }

    // Server sent a SIMPLE NOTIFY for message-summary (voicemail). PJSIP
    // hands us the parsed Messages-Waiting body in prm.rdata.wholeMsg.
    // We extract "Voice-Message: <new>/<old>" and stash it for the UI.
    void onMwiInfo(pj::OnMwiInfoParam &prm) override
    {
        const std::string body = prm.rdata.wholeMsg;
        int newCount = 0;
        int oldCount = 0;
        bool active = false;
        // Voice-Message line: "Voice-Message: N/M (urgent N/M)" — we only
        // care about the first integer pair.
        const auto pos = body.find("Voice-Message:");
        if (pos != std::string::npos) {
            const char *p = body.c_str() + pos + std::strlen("Voice-Message:");
            while (*p == ' ' || *p == '\t') ++p;
            sscanf(p, "%d/%d", &newCount, &oldCount);
            active = newCount > 0;
        } else {
            const auto a = body.find("Messages-Waiting:");
            if (a != std::string::npos) {
                active = body.find("yes", a) != std::string::npos;
            }
        }
        spdlog::info("Account {} MWI: new={} old={} active={}",
                     id, newCount, oldCount, active);
        if (owner) owner->updateMwi(id, newCount, oldCount, active);
    }
};

struct AccountsManager::Entry {
    Account account;
    std::unique_ptr<AccountImpl> impl;
};

AccountsManager::AccountsManager(SipEngine *engine,
                                 persistence::Database *db,
                                 platform::IKeychain *keychain)
    : m_engine(engine), m_db(db), m_keychain(keychain)
{
    loadFromDatabase();
    for (auto &e : m_entries) {
        if (e->account.enabled && e->account.registerOnStartup) {
            registerAccount(e->account.id);
        }
    }
}

AccountsManager::~AccountsManager()
{
    for (auto &e : m_entries) {
        if (e->impl) {
            try { e->impl->setRegistration(false); } catch (...) {}
        }
    }
}

void AccountsManager::loadFromDatabase()
{
    if (!m_db || !m_db->handle()) return;
    sqlite3_stmt *stmt = nullptr;
    const char *sql =
        "SELECT id, display_name, username, domain, auth_user, "
        "password_ref, transport, proxy, stun_server, "
        "register_on_startup, srtp_mode, allow_untrusted_cert, "
        "dtmf_method, is_default, enabled, sort_order, label, "
        "public_address, codecs, voicemail_number, "
        "register_interval_sec, keepalive_interval_sec, "
        "session_timers_enabled, publish_presence_enabled, "
        "ice_enabled, hide_caller_id, zrtp_enabled "
        "FROM accounts ORDER BY sort_order, id";
    if (sqlite3_prepare_v2(m_db->handle(), sql, -1, &stmt, nullptr) != SQLITE_OK) {
        spdlog::error("loadFromDatabase prepare failed: {}",
                      sqlite3_errmsg(m_db->handle()));
        return;
    }
    auto readText = [](sqlite3_stmt *s, int col) {
        const auto *t = reinterpret_cast<const char *>(sqlite3_column_text(s, col));
        return t ? std::string(t) : std::string{};
    };
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        auto entry = std::make_unique<Entry>();
        auto &a = entry->account;
        a.id              = sqlite3_column_int(stmt, 0);
        a.displayName     = readText(stmt, 1);
        a.username        = readText(stmt, 2);
        a.domain          = readText(stmt, 3);
        a.authUser        = readText(stmt, 4);
        a.passwordRef     = readText(stmt, 5);
        a.transport       = transportFromString(readText(stmt, 6));
        a.proxy           = readText(stmt, 7);
        a.stunServer      = readText(stmt, 8);
        a.registerOnStartup = sqlite3_column_int(stmt, 9) != 0;
        a.srtpMode        = srtpModeFromString(readText(stmt, 10));
        a.allowUntrustedCert = sqlite3_column_int(stmt, 11) != 0;
        a.dtmfMethod      = dtmfMethodFromString(readText(stmt, 12));
        a.isDefault       = sqlite3_column_int(stmt, 13) != 0;
        a.enabled         = sqlite3_column_int(stmt, 14) != 0;
        a.sortOrder       = sqlite3_column_int(stmt, 15);
        a.label           = readText(stmt, 16);
        if (a.label.empty()) a.label = a.displayName;
        a.publicAddress         = readText(stmt, 17);
        a.codecs                = readText(stmt, 18);
        a.voicemailNumber       = readText(stmt, 19);
        a.registerIntervalSec   = sqlite3_column_int(stmt, 20);
        a.keepaliveIntervalSec  = sqlite3_column_int(stmt, 21);
        a.sessionTimersEnabled  = sqlite3_column_int(stmt, 22) != 0;
        a.publishPresenceEnabled = sqlite3_column_int(stmt, 23) != 0;
        a.iceEnabled            = sqlite3_column_int(stmt, 24) != 0;
        a.hideCallerId          = sqlite3_column_int(stmt, 25) != 0;
        a.zrtpEnabled           = sqlite3_column_int(stmt, 26) != 0;
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
        "password_ref, transport, proxy, stun_server, public_address, codecs, "
        "voicemail_number, register_on_startup, register_interval_sec, "
        "keepalive_interval_sec, session_timers_enabled, publish_presence_enabled, "
        "ice_enabled, hide_caller_id, zrtp_enabled, srtp_mode, allow_untrusted_cert, "
        "dtmf_method, is_default, enabled, sort_order) "
        "VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)";
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
    bindText(stmt, 6, acc.passwordRef);
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
    sqlite3_bind_int(stmt, 24, acc.isDefault ? 1 : 0);
    sqlite3_bind_int(stmt, 25, acc.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 26, acc.sortOrder);
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
    if ((*it)->impl) {
        try { (*it)->impl->setRegistration(false); } catch (...) {}
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
        "transport = ?, proxy = ?, stun_server = ?, public_address = ?, "
        "codecs = ?, voicemail_number = ?, register_on_startup = ?, "
        "register_interval_sec = ?, keepalive_interval_sec = ?, "
        "session_timers_enabled = ?, publish_presence_enabled = ?, "
        "ice_enabled = ?, hide_caller_id = ?, zrtp_enabled = ?, "
        "srtp_mode = ?, allow_untrusted_cert = ?, dtmf_method = ?, "
        "enabled = ?, sort_order = ? WHERE id = ?";
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
    sqlite3_bind_text(stmt, 6, transportToString(acc.transport), -1, SQLITE_STATIC);
    bindText(stmt, 7, acc.proxy);
    bindText(stmt, 8, acc.stunServer);
    bindText(stmt, 9, acc.publicAddress);
    bindText(stmt, 10, acc.codecs);
    bindText(stmt, 11, acc.voicemailNumber);
    sqlite3_bind_int(stmt, 12, acc.registerOnStartup ? 1 : 0);
    sqlite3_bind_int(stmt, 13, acc.registerIntervalSec);
    sqlite3_bind_int(stmt, 14, acc.keepaliveIntervalSec);
    sqlite3_bind_int(stmt, 15, acc.sessionTimersEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 16, acc.publishPresenceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 17, acc.iceEnabled ? 1 : 0);
    sqlite3_bind_int(stmt, 18, acc.hideCallerId ? 1 : 0);
    sqlite3_bind_int(stmt, 19, acc.zrtpEnabled ? 1 : 0);
    sqlite3_bind_text(stmt, 20, srtpModeToString(acc.srtpMode), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 21, acc.allowUntrustedCert ? 1 : 0);
    sqlite3_bind_text(stmt, 22, dtmfMethodToString(acc.dtmfMethod), -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 23, acc.enabled ? 1 : 0);
    sqlite3_bind_int(stmt, 24, acc.sortOrder);
    sqlite3_bind_int(stmt, 25, acc.id);

    const bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    if (!ok) return false;

    const bool hadLiveAccount = (*it)->impl != nullptr;
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
    // Keep the same passwordRef so existing in-flight pj::AuthCredInfo
    // references and the DB row don't need to change.
    if (!m_keychain->set(e.account.passwordRef, password)) {
        spdlog::error("setPassword: keychain set failed");
        return false;
    }
    m_passwordCache[e.account.passwordRef] = password;
    // Rebuild the PJSIP account so the new credentials take effect now.
    const bool wasLive = e.impl != nullptr;
    if (wasLive) {
        unregisterAccount(id);
    }
    if (e.account.enabled && (wasLive || e.account.registerOnStartup)) {
        registerAccount(id);
    }
    return true;
}

bool AccountsManager::registerAccount(AccountId id)
{
    auto it = std::find_if(m_entries.begin(), m_entries.end(),
                           [id](const auto &e) { return e->account.id == id; });
    if (it == m_entries.end()) return false;
    auto &e = **it;
    if (!m_engine || !m_engine->isRunning()) return false;
    if (e.impl) return true; // already registered

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
            spdlog::error("registerAccount: keychain missing ref {}", e.account.passwordRef);
            return false;
        }
        password = *fetched;
        m_passwordCache.emplace(e.account.passwordRef, password);
    }

    pj::AccountConfig acfg;
    const bool tls = e.account.transport == Transport::Tls;
    const std::string scheme = tls ? "sips:" : "sip:";
    acfg.idUri = e.account.hideCallerId
        ? scheme + "anonymous@anonymous.invalid"
        : scheme + e.account.username + "@" + e.account.domain;
    acfg.regConfig.registrarUri =
        scheme + e.account.domain + (tls ? "" : transportScheme(e.account.transport));
    acfg.regConfig.timeoutSec = e.account.registerIntervalSec > 0
        ? e.account.registerIntervalSec : 300;
    const auto authUser = e.account.authUser.empty()
                              ? e.account.username
                              : e.account.authUser;
    pj::AuthCredInfo cred("digest", "*", authUser, 0, password);
    acfg.sipConfig.authCreds.push_back(cred);

    // Outbound proxy
    if (!e.account.proxy.empty()) {
        std::string proxy = e.account.proxy;
        if (proxy.rfind("sip:", 0) != 0 && proxy.rfind("sips:", 0) != 0) {
            proxy = scheme + proxy + transportScheme(e.account.transport);
        }
        acfg.sipConfig.proxies.push_back(proxy);
    }

    // NAT helpers
    if (!e.account.publicAddress.empty()) {
        acfg.sipConfig.contactForced = scheme + e.account.username + "@"
                                       + e.account.publicAddress;
    }
    // STUN: when present, request PJSIP to use the global STUN config for
    // this account. The STUN server itself must be set at endpoint init
    // time (SipEngine::start). Per-account dynamic STUN isn't exposed by
    // PJSUA2 — that's a v1 enhancement.
    if (!e.account.stunServer.empty()) {
        acfg.natConfig.sipStunUse = PJSUA_STUN_USE_DEFAULT;
        acfg.natConfig.mediaStunUse = PJSUA_STUN_USE_DEFAULT;
    }
    if (e.account.iceEnabled) {
        acfg.natConfig.iceEnabled = true;
    }
    acfg.regConfig.registerOnAdd = true;
    if (e.account.keepaliveIntervalSec > 0) {
        acfg.natConfig.udpKaIntervalSec = e.account.keepaliveIntervalSec;
    }
    if (!e.account.sessionTimersEnabled) {
        acfg.callConfig.timerUse = PJSUA_SIP_TIMER_INACTIVE;
    }
    acfg.presConfig.publishEnabled = e.account.publishPresenceEnabled;

    // Subscribe to message-summary so the server can push voicemail
    // notifications. PJSIP issues SUBSCRIBE shortly after REGISTER.
    acfg.mwiConfig.enabled = true;

    // SRTP per spec §3.1
    switch (e.account.srtpMode) {
    case SrtpMode::Disabled:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_DISABLED;
        break;
    case SrtpMode::Optional:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_OPTIONAL;
        break;
    case SrtpMode::Required:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_MANDATORY;
        break;
    }
    acfg.mediaConfig.srtpSecureSignaling = 0;

    // Per-account TLS verify: create a dedicated TLS transport with this
    // account's verify policy and bind the account to it via transportId.
    // PJSUA2 doesn't expose verifyServer on AccountSipConfig, so a per-
    // account transport is the supported way to control TLS verify.
    if (tls) {
        try {
            pj::TransportConfig tlsCfg;
            tlsCfg.port = 0;
            tlsCfg.tlsConfig.method = PJSIP_TLSV1_2_METHOD;
            tlsCfg.tlsConfig.verifyServer = !e.account.allowUntrustedCert;
            tlsCfg.tlsConfig.verifyClient = false;
            const auto tpId = pj::Endpoint::instance()
                .transportCreate(PJSIP_TRANSPORT_TLS, tlsCfg);
            acfg.sipConfig.transportId = tpId;
        } catch (const pj::Error &err) {
            spdlog::error("registerAccount: per-account TLS transport: {}",
                          err.info());
            return false;
        }
    }

    auto impl = std::make_unique<AccountImpl>();
    impl->id = id;
    impl->owner = this;
    try {
        impl->create(acfg);
    } catch (const pj::Error &err) {
        spdlog::error("registerAccount: pj create failed: {}", err.info());
        return false;
    }
    e.impl = std::move(impl);
    return true;
}

void AccountsManager::unregisterAccount(AccountId id)
{
    for (auto &e : m_entries) {
        if (e->account.id == id && e->impl) {
            try { e->impl->setRegistration(false); } catch (...) {}
            e->impl.reset();
            return;
        }
    }
}

MwiState AccountsManager::mwiStateOf(AccountId id) const
{
    auto it = m_mwi.find(id);
    return it == m_mwi.end() ? MwiState{} : it->second;
}

void AccountsManager::updateMwi(AccountId id, int newCount, int oldCount,
                                bool active)
{
    MwiState s;
    s.newMessages = newCount;
    s.oldMessages = oldCount;
    s.active = active;
    m_mwi[id] = s;
    if (m_onMwi) m_onMwi(id, s);
}

void AccountsManager::setOnMwiChanged(
    std::function<void(AccountId, MwiState)> cb)
{
    m_onMwi = std::move(cb);
}

bool AccountsManager::sendInstantMessage(AccountId accountId,
                                         const std::string &to,
                                         const std::string &body)
{
    for (auto &e : m_entries) {
        if (e->account.id != accountId || !e->impl) continue;
        // PJSUA2 only exposes sendInstantMessage on Buddy/Call (i.e. inside
        // an existing dialog/subscription). For one-shot out-of-dialog
        // MESSAGE the C API pjsua_im_send is the right tool.
        const int pjAccId = e->impl->getId();
        pj_str_t toStr  = pj_str(const_cast<char *>(to.c_str()));
        pj_str_t mime   = pj_str(const_cast<char *>("text/plain"));
        pj_str_t cont   = pj_str(const_cast<char *>(body.c_str()));
        const pj_status_t st = pjsua_im_send(pjAccId, &toStr, &mime,
                                             &cont, nullptr, nullptr);
        if (st != PJ_SUCCESS) {
            spdlog::error("AccountsManager::sendInstantMessage: pjsua_im_send={}",
                          st);
            return false;
        }
        return true;
    }
    return false;
}

void AccountsManager::setOnInstantMessage(
    std::function<void(AccountId, const std::string &,
                       const std::string &)> cb)
{
    m_onInstantMessage = std::move(cb);
}

void AccountsManager::reregisterAllEnabled()
{
    // Snapshot the IDs first — registerAccount can rebuild Entry::impl,
    // which would invalidate iterators if we walked m_entries directly.
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

RegistrationState AccountsManager::stateOf(AccountId id) const
{
    for (const auto &e : m_entries) {
        if (e->account.id == id) {
            return e->impl ? e->impl->state.load() : RegistrationState::Unregistered;
        }
    }
    return RegistrationState::Unregistered;
}

RegError AccountsManager::lastRegErrorOf(AccountId id) const
{
    for (const auto &e : m_entries) {
        if (e->account.id == id && e->impl) {
            std::lock_guard<std::mutex> lk(e->impl->errMutex);
            return e->impl->lastError;
        }
    }
    return {};
}

void AccountsManager::setOnRegistrationStateChanged(
    std::function<void(AccountId, RegistrationState)> cb)
{
    m_cb = std::move(cb);
}

void AccountsManager::setOnIncomingCall(std::function<void(AccountId, int)> cb)
{
    m_onIncoming = std::move(cb);
}

pj::Account *AccountsManager::pjAccountFor(AccountId id)
{
    for (auto &e : m_entries) {
        if (e->account.id == id) return e->impl.get();
    }
    return nullptr;
}

std::string AccountsManager::passwordRefFor(AccountId id) const
{
    for (const auto &e : m_entries) {
        if (e->account.id == id) return e->account.passwordRef;
    }
    return {};
}

} // namespace compactphone::sip

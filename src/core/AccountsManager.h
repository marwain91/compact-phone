#pragma once

#include "Account.h"
#include "sipbackend/ISipBackend.h"

#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// pj::Account is still needed by pjAccountFor() (transitional bridge until
// phase 3 when CallManager migrates off direct pj::Account access).
namespace pj { class Account; }

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }
namespace compactphone::sipbackend { class PjsipBackend; }

namespace compactphone::sip {

enum class RegistrationState {
    Unregistered,
    Registering,
    Registered,
    Failed,
};

// Last failure reported by the SIP server (or PJSIP itself for transport
// errors) on the most recent REGISTER attempt. `code` is the SIP status
// code (e.g. 401, 403, 408); 0 means PJSIP gave no code (typically a
// transport-level failure before any response arrived). `reason` is the
// human-readable phrase from the response. Cleared on a successful
// re-registration.
struct RegError {
    int code = 0;
    std::string reason;
    bool empty() const { return code == 0 && reason.empty(); }
};

// Result of mapping one REGISTER event: the externally visible state plus
// the lastError value that should be stored alongside it.
struct RegStateUpdate {
    RegistrationState state = RegistrationState::Unregistered;
    RegError error;
};

// Pure mapping from a REGISTER result to {state, lastError}. Inputs are
// pj::AccountInfo::regIsActive and the status code/reason from
// pj::OnRegStateParam (code 0 = no response yet / transport-level event).
// Error policy: Failed stores {code, reason}; Registered clears the error;
// Registering and Unregistered preserve `lastError` so the user can still
// read the failure reason while a retry is in flight. Extracted from
// AccountImpl::onRegState so the policy is unit-testable without a live
// PJSIP registration.
RegStateUpdate mapRegEvent(bool regIsActive, int statusCode,
                           const std::string &reason,
                           const RegError &lastError);

// Snapshot of the message-summary state reported by the server via MWI
// NOTIFY. newMessages == 0 with active == false means "no voicemail".
struct MwiState {
    int newMessages = 0;
    int oldMessages = 0;
    bool active = false;
};

// AccountsManager drives account registration through ISipBackend and
// receives queued main-thread events as an ISipBackendListener. It keeps
// ALL domain logic: DB CRUD, keychain, password cache, default-account
// policy, enable/update orchestration, and mapRegEvent application.
//
// Listener-side state (m_backendIds, m_regStates, m_regErrors) is
// MAIN-THREAD-ONLY: events arrive queued by the backend's EventDispatch, so
// no new mutex is needed for those maps. m_callbackMutex guards the four
// callback slots + m_mwi (phase 4 removes it when CallManager also rides
// the listener). m_backendIdsMutex guards m_backendIds only for the benefit
// of the incoming-call hook, which fires on the PJSIP thread.
class AccountsManager : public sipbackend::ISipBackendListener {
public:
    // pjsipBridge may be null (fake-backed tests and future non-PJSIP
    // backends). It exists solely for pjAccountFor() (phase-3-removed) and
    // for installing the native incoming-call hook (phase-2-only). The caller
    // (buildCoreSipGraph) must call backend->setListener(this) AFTER
    // construction and must call backend->setListener(nullptr) BEFORE
    // teardown — see CoreSipGraph.cpp.
    AccountsManager(sipbackend::ISipBackend *backend,
                    sipbackend::PjsipBackend *pjsipBridge,
                    persistence::Database *db,
                    platform::IKeychain *keychain);
    ~AccountsManager() override;

    AccountsManager(const AccountsManager &) = delete;
    AccountsManager &operator=(const AccountsManager &) = delete;

    // CRUD. Returns the new ID on add(), or kInvalidAccountId on failure.
    AccountId add(const Account &acc, const std::string &password);
    bool update(const Account &acc);
    bool remove(AccountId id);

    // Read access. Returns a copy.
    std::vector<Account> list() const;
    std::optional<Account> find(AccountId id) const;

    // Default account selection. Returns kInvalidAccountId if none.
    AccountId defaultAccountId() const;
    bool setDefault(AccountId id);

    bool setEnabled(AccountId id, bool enabled);

    // Replace the keychain-stored password for an account. Updates the
    // in-memory cache too. Re-registration happens here so PJSIP picks up
    // the new credentials immediately instead of failing on the next
    // refresh with stale auth.
    bool setPassword(AccountId id, const std::string &password);

    // Registration. Called automatically for enabled accounts on construction
    // and when add/update flips an account to enabled.
    bool registerAccount(AccountId id);
    void unregisterAccount(AccountId id);

    // Drop and re-create every enabled+registerOnStartup account's PJSIP
    // binding. Used after a network reachability change to force a fresh
    // REGISTER from the new source IP.
    void reregisterAllEnabled();

    RegistrationState stateOf(AccountId id) const;

    // Last registration error for an account. Returns an empty RegError if
    // the account is currently Registered or has never tried.
    RegError lastRegErrorOf(AccountId id) const;

    // Send a SIP MESSAGE (RFC 3428) from `accountId` to `to`. Returns
    // false if the account isn't registered or the backend refuses.
    bool sendInstantMessage(AccountId accountId, const std::string &to,
                            const std::string &body);

    // Callback fired on the main thread on every inbound MESSAGE.
    // Args: (account, fromUri, body).
    void setOnInstantMessage(
        std::function<void(AccountId, const std::string &,
                           const std::string &)> cb);

    // Latest MWI state for an account (zeroed default if unknown).
    MwiState mwiStateOf(AccountId id) const;

    // Internal: invoked from onMwi listener event. Stores state and
    // notifies any registered listener.
    void updateMwi(AccountId id, int newMessages, int oldMessages,
                   bool active);

    // Called on the main thread when message-summary changes.
    void setOnMwiChanged(std::function<void(AccountId, MwiState)> cb);

    // Callback fired on the main thread when any account's registration
    // state changes.
    void setOnRegistrationStateChanged(
        std::function<void(AccountId, RegistrationState)> cb);

    // Callback fired from a PJSIP thread (via native hook) when an inbound
    // call arrives. `pjsipCallId` is the native PJSUA call id, which
    // CallManager wraps via adoptIncomingCall.
    void setOnIncomingCall(std::function<void(AccountId, int)> cb);

    // For CallManager: returns the underlying pj::Account for the given id,
    // or nullptr if not registered. Phase-3-removed (CallManager migrates
    // onto the backend call API). Lifetime tied to AccountsManager.
    pj::Account *pjAccountFor(AccountId id);

    // Test hook: returns the keychain reference used for an account's
    // password (so tests can verify deletion).
    std::string passwordRefFor(AccountId id) const;

    // --- ISipBackendListener overrides (events arrive QUEUED on main thread) ---
    void onRegState(sipbackend::AccountId backendId, bool regActive,
                    int sipCode, const std::string &reason) override;
    void onMwi(sipbackend::AccountId backendId, int newMessages,
               int oldMessages, bool active) override;
    void onInstantMessage(sipbackend::AccountId backendId,
                          const std::string &fromUri,
                          const std::string &body) override;
    // Call-related listener events: empty in phase 2 (calls ride the native
    // hook; CallManager still uses pj::Account directly until phase 3).
    void onIncomingCall(sipbackend::AccountId, sipbackend::CallId,
                        const std::string &, const std::string &) override {}
    void onCallState(sipbackend::CallId, sipbackend::CallState,
                     int) override {}
    void onMediaState(sipbackend::CallId, bool, bool) override {}
    void onTransferStatus(sipbackend::CallId, int, bool,
                          const std::string &) override {}
    void onPresence(sipbackend::WatchId, sipbackend::PresenceState) override {}

private:
    struct Entry;

    sipbackend::ISipBackend *m_backend;
    // Nullable: non-null only for PjsipBackend-backed instances. Holds the
    // PJSIP-specific transitional bridges (pjAccountFor, incoming hook).
    // Phase-3-removed alongside pjAccountFor.
    sipbackend::PjsipBackend *m_pjsipBridge;
    persistence::Database *m_db;
    platform::IKeychain *m_keychain;

    std::vector<std::unique_ptr<Entry>> m_entries;
    // Plaintext password cache, keyed by Account::passwordRef. The first
    // successful keychain read populates the entry; later registerAccount /
    // reregisterAllEnabled / setEnabled cycles reuse it so the user is not
    // prompted by macOS Keychain Services more than once per session.
    std::unordered_map<std::string, std::string> m_passwordCache;

    // Maps domain AccountId → backend AccountId. Written on the main thread
    // by registerAccount/unregisterAccount; read on the PJSIP thread by the
    // incoming-call hook. Guarded by m_backendIdsMutex.
    //
    // THREE-LOCK ORDERING (must be observed everywhere, no exceptions):
    //   PjsipBackend::m_hookMutex → m_callbackMutex → m_backendIdsMutex
    //
    // The incoming-call path nests them in this order: PjsipBackend fires
    // the hook under m_hookMutex; the hook acquires m_backendIdsMutex for
    // the reverse-map lookup, then releases it before acquiring
    // m_callbackMutex to invoke m_onIncoming.  Never acquire these mutexes
    // in any other relative order, and never hold m_backendIdsMutex across
    // a callback invocation.
    //
    // m_backendIdsMutex discipline:
    //   - Hold BRIEFLY (map read/write only).
    //   - NEVER call into PJSIP or the backend while holding it.
    //   - The incoming-call hook holds it only long enough to do a
    //     reverse-map lookup, then releases before calling m_callbackMutex.
    mutable std::mutex m_backendIdsMutex;
    std::map<AccountId, sipbackend::AccountId> m_backendIds;

    // Main-thread-only: updated from the queued onRegState listener event.
    // No mutex needed — events arrive serialized on the main thread.
    std::map<AccountId, RegistrationState> m_regStates;
    std::map<AccountId, RegError>          m_regErrors;

    // Guards the four callback slots below plus m_mwi: assigned/read on the
    // main thread (controller ctors/dtors, mwiStateOf), invoked from various
    // threads (PJSIP hook for incoming calls; main thread for reg/MWI/IM
    // events). Invocation happens UNDER this mutex, so a setter call is a
    // quiesce barrier — when setOnX({}) returns, no in-flight invocation of
    // the previous callback exists and none can start.
    // FORBIDDEN from inside a callback: any setter (deadlock via self-lock)
    // AND any getter that takes this same mutex — mwiStateOf() in particular
    // acquires m_callbackMutex and will deadlock if called from a callback
    // handler that already holds it. Phase 4 removes this mutex when all
    // callbacks become main-thread-only listener events.
    mutable std::mutex m_callbackMutex;
    std::function<void(AccountId, RegistrationState)> m_cb;
    std::function<void(AccountId, int)> m_onIncoming;
    std::function<void(AccountId, MwiState)> m_onMwi;
    std::unordered_map<AccountId, MwiState> m_mwi;
    std::function<void(AccountId, const std::string &, const std::string &)>
        m_onInstantMessage;

    void loadFromDatabase();
    bool insertRow(Account &acc);
    bool deleteRow(AccountId id);
};

} // namespace compactphone::sip

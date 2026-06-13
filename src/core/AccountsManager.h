#pragma once

#include "Account.h"
#include "sipbackend/ISipBackend.h"

#include <QObject>

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

// pj::Account is still needed by pjAccountFor(), which LinesManager uses to
// drive presence (BLF) pj::Buddy subscriptions. Presence moves behind the
// backend in phase 5; pjAccountFor and this forward declaration die then.
namespace pj { class Account; }

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }

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
// MAIN-THREAD-ONLY since phase 3 (the PJSIP-thread incoming-call hook is
// gone): all listener-side state (m_backendIds, m_regStates, m_regErrors,
// m_mwi) is read and written on the main thread, so no mutex guards them.
// Registration / MWI / instant-message events are published as Qt signals,
// connected directly on the main thread.
class AccountsManager : public QObject,
                        public sipbackend::ISipBackendListener {
    Q_OBJECT
signals:
    // Fired on the main thread when any account's registration state changes
    // (from onRegState). Connect directly — no metatype registration needed.
    void registrationStateChanged(AccountId id, RegistrationState state);
    // Fired on the main thread when an account's MWI (message-summary)
    // state changes (from onMwi/updateMwi).
    void mwiChanged(AccountId id, MwiState state);
    // Fired on the main thread on every inbound MESSAGE (from
    // onInstantMessage). Args: (account, fromUri, body).
    void instantMessageReceived(AccountId id, const std::string &fromUri,
                                const std::string &body);

public:
    // The caller (buildCoreSipGraph) must call backend->setListener(this)
    // AFTER construction and must call backend->setListener(nullptr) BEFORE
    // teardown — see CoreSipGraph.cpp.
    AccountsManager(sipbackend::ISipBackend *backend,
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

    // Registration. registerAccount and unregisterAccount are called
    // automatically when add/update/setEnabled flips account state.
    // registerStartupAccounts() registers all enabled+registerOnStartup
    // accounts; it must be called AFTER the backend listener is installed
    // (buildCoreSipGraph and SipManagerPair do this — callers must not
    // call it before backend->setListener(this)).
    void registerStartupAccounts();
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

    // Latest MWI state for an account (zeroed default if unknown).
    MwiState mwiStateOf(AccountId id) const;

    // Internal: invoked from onMwi listener event. Stores state and
    // emits mwiChanged.
    void updateMwi(AccountId id, int newMessages, int oldMessages,
                   bool active);

    // Backend id for a registered domain account (kInvalidAccountId if not
    // registered). Main-thread-only. Used by CallManager::makeCall.
    sipbackend::AccountId backendIdFor(AccountId id) const;
    // Reverse mapping for backend events that carry a backend account id.
    // Main-thread-only.
    AccountId accountIdForBackend(sipbackend::AccountId backendId) const;

    // For LinesManager presence (BLF): returns the underlying pj::Account for
    // the given id, or nullptr if not registered or the backend is not the
    // PJSIP adapter. Resolved via a dynamic_cast on the backend. Removed in
    // phase 5 when presence moves behind ISipBackend. Lifetime tied to
    // AccountsManager.
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
    // Call-related listener events: AccountsManager handles only the account
    // half; the ListenerFanout forwards these to CallManager, which owns them.
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
    persistence::Database *m_db;
    platform::IKeychain *m_keychain;

    std::vector<std::unique_ptr<Entry>> m_entries;
    // Plaintext password cache, keyed by Account::passwordRef. The first
    // successful keychain read populates the entry; later registerAccount /
    // reregisterAllEnabled / setEnabled cycles reuse it so the user is not
    // prompted by macOS Keychain Services more than once per session.
    std::unordered_map<std::string, std::string> m_passwordCache;

    // Maps domain AccountId → backend AccountId. Main-thread-only since
    // phase 3 (the PJSIP-thread incoming hook that needed a mutex is gone):
    // written by registerAccount/unregisterAccount and read by
    // backendIdFor/accountIdForBackend/sendInstantMessage/pjAccountFor, all
    // on the main thread.
    std::map<AccountId, sipbackend::AccountId> m_backendIds;

    // Main-thread-only: updated from the queued onRegState listener event.
    // No mutex needed — events arrive serialized on the main thread.
    std::map<AccountId, RegistrationState> m_regStates;
    std::map<AccountId, RegError>          m_regErrors;

    // Latest MWI state per account — main-thread-only, written by updateMwi
    // and read by mwiStateOf. No mutex: every reg/MWI/IM event is a queued
    // main-thread listener call that re-emits as a direct Qt signal.
    std::unordered_map<AccountId, MwiState> m_mwi;

    void loadFromDatabase();
    bool insertRow(Account &acc);
    bool deleteRow(AccountId id);
};

} // namespace compactphone::sip

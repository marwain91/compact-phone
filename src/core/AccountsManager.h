#pragma once

#include "Account.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace pj { class Account; }

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }

namespace compactphone::sip {

class SipEngine;

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

// Snapshot of the message-summary state reported by the server via MWI
// NOTIFY. newMessages == 0 with active == false means "no voicemail".
struct MwiState {
    int newMessages = 0;
    int oldMessages = 0;
    bool active = false;
};

class AccountsManager {
public:
    AccountsManager(SipEngine *engine,
                    persistence::Database *db,
                    platform::IKeychain *keychain);
    ~AccountsManager();

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
    // false if the account isn't registered or PJSIP refuses.
    bool sendInstantMessage(AccountId accountId, const std::string &to,
                            const std::string &body);

    // Callback fired from a PJSIP thread on every inbound MESSAGE.
    // Args: (account, fromUri, body). Marshal to your thread.
    void setOnInstantMessage(
        std::function<void(AccountId, const std::string &,
                           const std::string &)> cb);

    // Latest MWI state for an account (zeroed default if unknown).
    MwiState mwiStateOf(AccountId id) const;

    // Internal: invoked from AccountImpl::onMwiInfo. Stores state and
    // notifies any registered listener.
    void updateMwi(AccountId id, int newMessages, int oldMessages,
                   bool active);

    // Called from a PJSIP thread when message-summary changes. Marshal
    // to your thread before touching shared state.
    void setOnMwiChanged(std::function<void(AccountId, MwiState)> cb);

    // Callback fired from a PJSIP thread when any account's registration
    // state changes. Marshal to your thread before touching shared state.
    void setOnRegistrationStateChanged(
        std::function<void(AccountId, RegistrationState)> cb);

    // Callback fired from a PJSIP thread when an inbound call arrives.
    // `pjsipCallId` is the native PJSUA call id, which CallManager wraps
    // via adoptIncomingCall.
    void setOnIncomingCall(std::function<void(AccountId, int)> cb);

    // For CallManager: returns the underlying pj::Account for the given id,
    // or nullptr if not registered. Lifetime tied to AccountsManager.
    pj::Account *pjAccountFor(AccountId id);

    // Test hook: returns the keychain reference used for an account's
    // password (so tests can verify deletion).
    std::string passwordRefFor(AccountId id) const;

private:
    class AccountImpl;
    struct Entry;

    SipEngine *m_engine;
    persistence::Database *m_db;
    platform::IKeychain *m_keychain;

    std::vector<std::unique_ptr<Entry>> m_entries;
    // Plaintext password cache, keyed by Account::passwordRef. The first
    // successful keychain read populates the entry; later registerAccount /
    // reregisterAllEnabled / setEnabled cycles reuse it so the user is not
    // prompted by macOS Keychain Services more than once per session. The
    // password is already held in PJSIP's pj::AuthCredInfo for the lifetime
    // of the pj::Account, so this cache is not a fresh secrecy regression.
    std::unordered_map<std::string, std::string> m_passwordCache;
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

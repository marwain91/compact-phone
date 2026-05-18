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

    // Registration. Called automatically for enabled accounts on construction
    // and when add/update flips an account to enabled.
    bool registerAccount(AccountId id);
    void unregisterAccount(AccountId id);

    // Drop and re-create every enabled+registerOnStartup account's PJSIP
    // binding. Used after a network reachability change to force a fresh
    // REGISTER from the new source IP.
    void reregisterAllEnabled();

    RegistrationState stateOf(AccountId id) const;

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

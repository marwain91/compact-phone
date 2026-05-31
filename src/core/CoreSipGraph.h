#pragma once

#include <memory>

class QObject;

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }
namespace compactphone::sip {
class SipEngine;
class AccountsManager;
class CallManager;
}
namespace compactphone::models { class AccountsModel; }

namespace compactphone {

class AccountsController;

// The SIP-account core shared by both entrypoints: the accounts manager, its
// list model, the accounts controller, and the call manager. The GUI
// (PhoneController) and the headless runner each built this same four-object
// graph by hand, which is exactly the kind of composition that drifts when a
// constructor signature changes. buildCoreSipGraph() is the single place that
// wires it.
struct CoreSipGraph {
    std::unique_ptr<sip::AccountsManager>  accounts;
    std::unique_ptr<models::AccountsModel> accountsModel;
    std::unique_ptr<AccountsController>    accountsController;
    std::unique_ptr<sip::CallManager>      calls;
};

// Build the shared core against an already-started engine, an open database,
// and a keychain — all owned by the caller, whose lifetimes must outlive the
// returned graph. `parent` is the QObject parent for the model/controller
// (PhoneController passes itself; the headless runner passes nullptr and owns
// them through the returned unique_ptrs). The caller does any additional
// signal wiring and layers the GUI-only CallsModel/CallsController on top.
CoreSipGraph buildCoreSipGraph(sip::SipEngine *engine,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               QObject *parent = nullptr);

} // namespace compactphone

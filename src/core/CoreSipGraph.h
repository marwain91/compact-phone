#pragma once

#include <memory>

class QObject;

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }
namespace compactphone::sip {
class AccountsManager;
class CallManager;
}
namespace compactphone::models { class AccountsModel; }
namespace compactphone::sipbackend {
class ISipBackend;
class ListenerFanout;
}

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
    // The fanout that routes backend events to accounts (first) then calls.
    // Declared LAST so it is destroyed FIRST — safe because the caller must
    // call backend->setListener(nullptr) before the graph dies (wiring
    // contract below), after which the fanout has no live source.
    std::unique_ptr<sipbackend::ListenerFanout> listener;
};

// Build the shared core against an already-started backend, an open database,
// and a keychain — all owned by the caller, whose lifetimes must outlive the
// returned graph. AccountsController pushes STUN / codec-priority settings to
// the backend whenever accounts are added, updated, enabled, or set as
// default. `parent` is the QObject parent for the model/controller
// (PhoneController passes itself; the headless runner passes nullptr and owns
// them through the returned unique_ptrs). The caller does any additional
// signal wiring and layers the GUI-only CallsModel/CallsController on top.
//
// Wiring contract:
//   - buildCoreSipGraph constructs a ListenerFanout (accounts then calls) and
//     calls backend->setListener(graph.listener.get()).
//   - The caller MUST keep graph.listener alive (move it into a member) and
//     MUST call backend->setListener(nullptr) BEFORE the graph dies (i.e.
//     before the managers it points at are destroyed). In PhoneController this
//     belongs at the top of the destructor, before any managers are reset.
//     In HeadlessRunner it belongs in the destructor or in stop() before
//     the graph unique_ptrs are released.
//
CoreSipGraph buildCoreSipGraph(sipbackend::ISipBackend *backend,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               QObject *parent = nullptr);

} // namespace compactphone

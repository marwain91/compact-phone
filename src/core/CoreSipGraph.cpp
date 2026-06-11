#include "CoreSipGraph.h"

#include "AccountsController.h"
#include "AccountsManager.h"
#include "CallManager.h"
#include "models/AccountsModel.h"
#include "sipbackend/ISipBackend.h"
#include "sipbackend/pjsip/PjsipBackend.h"

namespace compactphone {

CoreSipGraph buildCoreSipGraph(sipbackend::ISipBackend *backend,
                               sipbackend::PjsipBackend *pjsipBridge,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               QObject *parent)
{
    CoreSipGraph g;
    g.accounts = std::make_unique<sip::AccountsManager>(
        backend, pjsipBridge, db, keychain);
    // Wire the listener AFTER construction so the manager is fully constructed
    // when the first queued event arrives. The caller must call
    // backend->setListener(nullptr) before the graph is torn down —
    // see CoreSipGraph.h's wiring contract.
    backend->setListener(g.accounts.get());
    g.accountsModel =
        std::make_unique<models::AccountsModel>(g.accounts.get(), parent);
    g.accountsController = std::make_unique<AccountsController>(
        g.accounts.get(), g.accountsModel.get(), nullptr, parent);
    g.calls = std::make_unique<sip::CallManager>(g.accounts.get());
    return g;
}

} // namespace compactphone

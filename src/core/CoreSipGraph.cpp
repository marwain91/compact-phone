#include "CoreSipGraph.h"

#include "AccountsController.h"
#include "AccountsManager.h"
#include "CallManager.h"
#include "SipEngine.h"
#include "models/AccountsModel.h"
#include "sipbackend/ISipBackend.h"
#include "sipbackend/pjsip/PjsipBackend.h"

namespace compactphone {

CoreSipGraph buildCoreSipGraph(sipbackend::ISipBackend *backend,
                               sipbackend::PjsipBackend *pjsipBridge,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               sip::SipEngine *engine,
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
    // engine is passed through so AccountsController::pushNetworkAndCodecSettings
    // can apply STUN / codec-priority on every account add/update/enable/default
    // change. Pass nullptr only in unit tests that use a fake backend.
    g.accountsController = std::make_unique<AccountsController>(
        g.accounts.get(), g.accountsModel.get(), engine, parent);
    g.calls = std::make_unique<sip::CallManager>(g.accounts.get());
    return g;
}

} // namespace compactphone

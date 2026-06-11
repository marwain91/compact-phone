#include "CoreSipGraph.h"

#include "AccountsController.h"
#include "AccountsManager.h"
#include "CallManager.h"
#include "SipEngine.h"
#include "models/AccountsModel.h"
#include "sipbackend/ISipBackend.h"
#include "sipbackend/ListenerFanout.h"

#include <vector>

namespace compactphone {

CoreSipGraph buildCoreSipGraph(sipbackend::ISipBackend *backend,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               sip::SipEngine *engine,
                               QObject *parent)
{
    CoreSipGraph g;
    g.accounts = std::make_unique<sip::AccountsManager>(backend, db, keychain);
    g.calls = std::make_unique<sip::CallManager>(backend, g.accounts.get());
    // The fanout routes every backend event to accounts FIRST (registration
    // bookkeeping current) then calls. Wire the listener AFTER both managers
    // exist so the first queued event has live sinks.
    // registerStartupAccounts() runs immediately after so events from the
    // first REGISTER response have a live listener — this closes the window
    // where a fast registrar's first onRegState lands before anyone listens.
    // The caller MUST call backend->setListener(nullptr) before the graph is
    // torn down — see CoreSipGraph.h's wiring contract.
    g.listener = std::make_unique<sipbackend::ListenerFanout>(
        std::vector<sipbackend::ISipBackendListener *>{
            g.accounts.get(), g.calls.get()});   // accounts BEFORE calls
    backend->setListener(g.listener.get());
    g.accounts->registerStartupAccounts();
    g.accountsModel =
        std::make_unique<models::AccountsModel>(g.accounts.get(), parent);
    // engine is passed through so AccountsController::pushNetworkAndCodecSettings
    // can apply STUN / codec-priority on every account add/update/enable/default
    // change. Pass nullptr only in unit tests that use a fake backend.
    g.accountsController = std::make_unique<AccountsController>(
        g.accounts.get(), g.accountsModel.get(), engine, parent);
    return g;
}

} // namespace compactphone

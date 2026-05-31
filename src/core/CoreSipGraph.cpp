#include "CoreSipGraph.h"

#include "AccountsController.h"
#include "AccountsManager.h"
#include "CallManager.h"
#include "models/AccountsModel.h"

namespace compactphone {

CoreSipGraph buildCoreSipGraph(sip::SipEngine *engine,
                               persistence::Database *db,
                               platform::IKeychain *keychain,
                               QObject *parent)
{
    CoreSipGraph g;
    g.accounts = std::make_unique<sip::AccountsManager>(engine, db, keychain);
    g.accountsModel =
        std::make_unique<models::AccountsModel>(g.accounts.get(), parent);
    g.accountsController = std::make_unique<AccountsController>(
        g.accounts.get(), g.accountsModel.get(), engine, parent);
    g.calls = std::make_unique<sip::CallManager>(g.accounts.get());
    return g;
}

} // namespace compactphone

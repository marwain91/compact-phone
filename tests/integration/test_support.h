#pragma once

// TSan-safe wait/observation helpers shared by the integration suite.
//
// The rule these helpers encode: never block on a condition_variable whose
// mutex is also locked by a PJSIP-delivered callback. PJSUA can dispatch
// callbacks re-entrantly on the waiting/registering thread, which TSan
// reports as a double lock of the test mutex — and once TSan considers a
// mutex misused, every later access through it is reported as a data race
// too (this accounted for all 29 baseline failures when the gate widened
// from ThreadStressTest to the full suite). Safe shapes instead:
//
//   - registration waits: poll AccountsManager::stateOf()
//     with no callback installed at all — waitForRegState below
//   - scalar observations: std::atomic written in the callback, polled here
//   - keyed observations (per-call state maps): a plain mutex held briefly
//     by both the callback and the polling predicate, but never slept on
//
// Lifetime rule that goes with them: observation state shared with a
// connected slot must be declared BEFORE the manager whose signal it
// observes, so it outlives the connection — which is severed only when
// that manager (the signal's sender) is destroyed.
//
// Phase-3 note: integration tests construct their full SIP stack via the
// SipManagerPair helper below — a PjsipBackend paired with an AccountsManager,
// a CallManager, and the ListenerFanout that splits backend events between
// them. Call events (incoming announcements, call-state, media, transfer) are
// now queued main-thread deliveries through the backend's EventDispatch, just
// like registration state: NOTHING at manager altitude advances unless the Qt
// event loop pumps. So ALL waits on manager-visible call state must pump
// (pumpUntil), not sleep-poll. Pumping from the main thread cannot re-enter
// PJSIP callbacks, because those callbacks stop at the adapter's queue boundary
// — they only enqueue an event, they never call up into a listener inline.

#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
#include "core/sipbackend/ListenerFanout.h"
#include "core/sipbackend/pjsip/PjsipBackend.h"
#include "core/platform/Keychain.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <chrono>
#include <initializer_list>
#include <thread>
#include <vector>

namespace compactphone::testsupport {

// Pairs a PjsipBackend with the full main-thread SIP stack — an
// AccountsManager and a CallManager — fanned out through a ListenerFanout,
// matching the buildCoreSipGraph wiring order (setListener then
// registerStartupAccounts). Integration tests that need a full engine + call
// stack construct this helper in their fixtures or test bodies instead of
// building the graph by hand. Use `smp.calls` as the CallManager — a
// privately-constructed CallManager would not be wired into the fanout and
// would never receive an event.
//
// Destruction order: fanout's listener slot is cleared first (so no queued
// event reaches a half-destroyed sink), then calls, manager, fanout, backend
// destruct in declaration-reverse order. The backend does not call
// engine->stop().
struct SipManagerPair {
    sipbackend::PjsipBackend   backend;
    sip::AccountsManager       manager;
    sip::CallManager           calls;
    sipbackend::ListenerFanout fanout;

    SipManagerPair(sip::SipEngine *engine,
                   persistence::Database *db,
                   platform::IKeychain *kc)
        : backend(engine)
        , manager(&backend, db, kc)
        , calls(&backend, &manager)
        , fanout(std::vector<sipbackend::ISipBackendListener *>{&manager,
                                                                &calls})
    {
        // setListener first so queued reg-state/call events have a
        // destination, then registerStartupAccounts() — mirrors
        // buildCoreSipGraph order. Accounts BEFORE calls in the fanout so an
        // account's bookkeeping is current before any call event referencing
        // it is handled.
        backend.setListener(&fanout);
        manager.registerStartupAccounts();
    }
    ~SipManagerPair()
    {
        // Quiesce listener before the sinks destruct.
        backend.setListener(nullptr);
    }
    SipManagerPair(const SipManagerPair &) = delete;
    SipManagerPair &operator=(const SipManagerPair &) = delete;
};

// Pumps the Qt event loop each iteration so queued
// main-thread work (CallManager's invokeMethod handlers, retry timers,
// the post-disconnect grace timer) actually runs. Requires a live
// QCoreApplication. The 20ms sleep keeps the instrumented loop from
// monopolizing the PJSUA lock under TSan (which livelocks the worker).
template <typename Pred>
bool pumpUntil(Pred pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (pred()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return pred();
}

// Waits until stateOf(id) == want for every id. Registration state changes
// arrive as queued main-thread events (AccountsManager::onRegState is posted
// via the backend's EventDispatch and runs only when the Qt event loop pumps),
// so the wait uses pumpUntil — pumping from the main thread cannot re-enter
// PJSIP callbacks because those now stop at the adapter's queue boundary.
// Tracking CURRENT state (not transition counts) also makes the wait immune to
// registration flaps — an account that registers, drops, and re-registers can
// satisfy a "saw Registered N times" count while another account is still
// unregistered (the CallPoliciesTest flake).
inline bool waitForRegState(
    sip::AccountsManager &am,
    std::initializer_list<sip::AccountId> ids,
    sip::RegistrationState want,
    std::chrono::milliseconds timeout)
{
    return pumpUntil([&] {
        for (const auto id : ids) {
            if (am.stateOf(id) != want) return false;
        }
        return true;
    }, timeout);
}

} // namespace compactphone::testsupport

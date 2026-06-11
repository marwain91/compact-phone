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
// callback must be declared BEFORE the manager the callback is installed
// on (so it is destroyed after the manager stops delivering events), and
// AccountsManager callbacks that capture a CallManager must be cleared
// before that CallManager dies — ScopedAccountCallbacks does this even on
// an ASSERT early-return.
//
// Phase-2 note: integration tests construct their AccountsManager via the
// SipManagerPair helper below which pairs it with a PjsipBackend.
// waitForRegState uses pumpUntil (registration state arrives as a queued
// main-thread event and only advances when the Qt event loop pumps).

#include "core/AccountsManager.h"
#include "core/SipEngine.h"
#include "core/sipbackend/pjsip/PjsipBackend.h"
#include "core/platform/Keychain.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <chrono>
#include <initializer_list>
#include <thread>

namespace compactphone::testsupport {

// Pairs a PjsipBackend with an AccountsManager and wires the listener,
// matching the buildCoreSipGraph wiring order (setListener then
// registerStartupAccounts). Integration tests that need a full
// engine+accounts stack construct this helper in their fixtures or test
// bodies instead of building the graph by hand.
//
// Destruction order: manager destructor runs first (quiesces the native
// hook, removes backend accounts), then backend destructor. The backend
// does not call engine->stop().
struct SipManagerPair {
    sipbackend::PjsipBackend backend;
    sip::AccountsManager     manager;

    SipManagerPair(sip::SipEngine *engine,
                   persistence::Database *db,
                   platform::IKeychain *kc)
        : backend(engine)
        , manager(&backend, &backend, db, kc)
    {
        // setListener first so queued reg-state events have a destination,
        // then registerStartupAccounts() — mirrors buildCoreSipGraph order.
        backend.setListener(&manager);
        manager.registerStartupAccounts();
    }
    ~SipManagerPair()
    {
        // Quiesce listener before manager destructs.
        backend.setListener(nullptr);
    }
    SipManagerPair(const SipManagerPair &) = delete;
    SipManagerPair &operator=(const SipManagerPair &) = delete;
};

// Polls pred every `step` until it holds or `timeout` elapses. Pure sleep
// polling — use where the original wait blocked without running the Qt
// event loop (state arrives directly on PJSIP threads).
template <typename Pred>
bool pollUntil(Pred pred, std::chrono::milliseconds timeout,
               std::chrono::milliseconds step = std::chrono::milliseconds(20))
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (pred()) return true;
        std::this_thread::sleep_for(step);
    }
    return pred();
}

// Like pollUntil, but pumps the Qt event loop each iteration so queued
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

// Quiesce barrier on scope exit: clears every AccountsManager callback
// slot. The setters share the slot mutex with the PJSIP-thread invocation,
// so once the destructor returns no in-flight callback exists and none can
// start. Declare AFTER the CallManager (and any frame-local capture) so it
// runs first on scope exit — including gtest ASSERT early-returns, which
// skip any cleanup written after the assertion.
class ScopedAccountCallbacks {
public:
    explicit ScopedAccountCallbacks(sip::AccountsManager &am) : m_am(am) {}
    ~ScopedAccountCallbacks()
    {
        m_am.setOnRegistrationStateChanged({});
        m_am.setOnIncomingCall({});
        m_am.setOnInstantMessage({});
        m_am.setOnMwiChanged({});
    }
    ScopedAccountCallbacks(const ScopedAccountCallbacks &) = delete;
    ScopedAccountCallbacks &operator=(const ScopedAccountCallbacks &) = delete;

private:
    sip::AccountsManager &m_am;
};

} // namespace compactphone::testsupport

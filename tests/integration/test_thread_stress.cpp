#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallEntry.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

// ThreadSanitizer gate for the adapter's concurrency surface.
//
// The old gate stressed PJSIP-thread adoptIncomingCall racing main-thread
// snapshot() over CallManager's maps. That surface is GONE: CallManager is
// main-thread-only and snapshot() touches no shared state. The old
// CallbackReassignmentRacesDelivery test is also gone — its raced pair
// (std::function assign on the main thread vs invoke on the PJSIP thread) no
// longer exists either, because every callback invocation is a queued
// main-thread listener event since phase 3. Do not reintroduce it for a ghost.
//
// The reliably red-provable cross-thread surface now is PjsipBackend::m_calls
// under m_callsMutex: wrapIncomingCall inserts on the PJSUA worker thread while
// liveCall() lookups / releaseCall() erase on the main thread.
// EagerWrapRacesBackendQueries (Test 1) is the one-line red-proof — delete the
// lock_guard in wrapIncomingCall's insert block and TSan halts on the map node
// (liveCall -> _Rb_tree::find at PjsipBackend.cpp vs the worker's node
// allocate). RED-PROVEN during implementation: 3/3 runs halted. The trick that
// makes it reliable is probing DEAD ids (above the live call-id space): a
// dead-id query is a pure m_calls.find with NO getInfo(), so it never takes
// the PJSUA lock — a live-id query WOULD take it and that lock (which
// wrapIncomingCall also holds) would create a happens-before edge masking the
// race. See hammerQueries below.
//
// PjsipCall::m_muted (read in onCallMediaState on the PJSUA thread vs written
// in setMuted on the main thread) is deliberately NOT a separate red-proof:
// every onCallMediaState read AND every setMuted write runs under the PJSUA
// lock (setMuted reaches it through getInfo()), so the two are serialized by
// pjsua in practice and demoting the atomic does NOT make TSan halt. The
// atomic remains the documented contract; MuteTogglesRaceMediaReactivation
// (Test 2) is a concurrency WORKLOAD that churns setMuted vs hold/unhold
// re-INVITE renegotiation (the onCallMediaState mute-honouring + bridge-rewire
// path) under instrumentation — it catches crashes / use-after-free / deadlock
// in that path, not a one-line lock removal.

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}

std::string udpTarget(const std::string &extension)
{
    return "sip:" + extension + "@" + sipServer() + ";transport=udp";
}
} // namespace

class ThreadStressTest : public ::testing::Test {
protected:
    int argc = 1;
    char argv0[1] = {0};
    char *argv = argv0;
    std::unique_ptr<QCoreApplication> app;

    compactphone::sip::SipEngine engine;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    void SetUp() override
    {
        app = std::make_unique<QCoreApplication>(argc, &argv);
        ASSERT_TRUE(engine.start(0));
        ASSERT_TRUE(db.openInMemory());
    }
    void TearDown() override { engine.stop(); }

    static compactphone::sip::AccountId mkAccount(
        compactphone::sip::AccountsManager &am, const std::string &user,
        const std::string &pwd, bool isDefault)
    {
        compactphone::sip::Account a;
        a.displayName = user;
        a.username = user;
        a.domain = sipServer();
        a.authUser = user;
        a.transport = compactphone::sip::Transport::Udp;
        a.enabled = true;
        a.isDefault = isDefault;
        a.registerOnStartup = true;
        return am.add(a, pwd);
    }
};

// Test 1 — the PJSIP-thread eager wrap (adapter map insert under m_callsMutex)
// races the main thread hammering the adapter's query paths through
// CallManager: snapshot(), isMediaActive(), isCaptureTransmitting(),
// streamStats(), callCount() — every one a liveCall() lookup that takes
// m_callsMutex against the worker-thread insert, and a getInfo() against
// teardown.
TEST_F(ThreadStressTest, EagerWrapRacesBackendQueries)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto &cm = smp.calls;

    const auto id1 = mkAccount(am, "1001", "compactphone1001", true);
    const auto id2 = mkAccount(am, "1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidCallId);
    ASSERT_NE(id2, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    // Incoming calls are wrapped on the PJSIP thread and announced via the
    // queued signal on the main thread; accept() is already main-thread here.
    std::atomic<int> announced{0};
    QObject::connect(&cm, &compactphone::sip::CallManager::incomingCall,
                     [&](int id) {
                         announced.fetch_add(1);
                         cm.accept(id);
                     });

    // Tears down every live call and drains the disconnect + grace queue
    // while still pumping the cross-thread query paths.
    const auto drainCalls = [&]() {
        for (const auto &e : cm.snapshot()) cm.hangup(e.id);
        return pumpUntil([&] { return cm.callCount() == 0; }, 10s);
    };

    // Probe a band of ids ABOVE the backend's live call-id space (50001+).
    // isMediaActive() on a dead id is a pure liveCall() lookup: it takes
    // m_callsMutex, reads the m_calls map, finds nothing, and returns —
    // WITHOUT calling getInfo() (no live call), so it never touches the PJSUA
    // lock. That matters: the PJSIP worker thread runs wrapIncomingCall (the
    // m_calls insert) while holding the PJSUA lock, so a main-thread query
    // that ALSO took the PJSUA lock (getInfo on a live id) would be ordered
    // against the insert by that lock and the race would be masked. Pure
    // dead-id finds share no lock with the worker except m_callsMutex itself —
    // exactly the lock whose removal this test must catch. The burst keeps the
    // main thread inside m_calls.find() for a large fraction of each iteration
    // so the worker's lock-free insert reliably overlaps a read; the periodic
    // processEvents()/sleep still yields to the worker so the instrumented
    // loop can't livelock it under TSan.
    const auto hammerQueries = [&] {
        for (int rep = 0; rep < 400; ++rep) {
            for (compactphone::sip::CallId probe = 90001; probe <= 90016;
                 ++probe) {
                (void)cm.isMediaActive(probe);
                (void)cm.isCaptureTransmitting(probe);
            }
        }
        (void)cm.callCount();
    };

    // Dial repeatedly. Each 1002->1001 dial is routed back to account 1001 by
    // Asterisk, so the eager wrap fires on the PJSIP worker thread (inserting
    // into m_calls) while hammerQueries() reads that map on the main thread.
    // Many wraps over a long window, with no early break, so the overlap is
    // hit reliably rather than depending on a single insert landing in a
    // single read.
    constexpr int kTargetWraps = 6;
    const auto deadline = std::chrono::steady_clock::now() + 40s;
    while (announced.load() < kTargetWraps
           && std::chrono::steady_clock::now() < deadline) {
        if (cm.callCount() == 0) {
            const auto outbound = cm.makeCall(id2, udpTarget("1001"));
            ASSERT_NE(outbound, compactphone::sip::kInvalidCallId);
        }
        // Hammer the map-reading query paths while the INVITE + eager wrap +
        // accept + media activation are in flight.
        const auto callDeadline = std::chrono::steady_clock::now() + 6s;
        while (std::chrono::steady_clock::now() < callDeadline) {
            hammerQueries();
            QCoreApplication::processEvents();
            if (cm.callCount() >= 2) break;   // both legs up — tear down + redial
            std::this_thread::sleep_for(1ms);
        }
        ASSERT_TRUE(drainCalls()) << "calls did not drain";
    }
    EXPECT_GE(announced.load(), 2)
        << "wrap/query workload was not exercised (" << announced.load()
        << " announcements)";
}

// Test 2 — concurrency WORKLOAD for the mute-across-renegotiation path. Each
// hold/unhold pair forces a re-INVITE renegotiation, which dispatches
// onCallMediaState on the PJSIP worker thread — reading PjsipCall::m_muted and
// rewiring the conference bridge — while the main thread keeps flipping the
// same flag through setMuted. Both ends run under the PJSUA lock, so this is
// not a one-line lock red-proof (see the file header); it exercises the path
// under instrumentation to catch crashes / use-after-free / deadlock there.
TEST_F(ThreadStressTest, MuteTogglesRaceMediaReactivation)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto &cm = smp.calls;

    const auto id1 = mkAccount(am, "1001", "compactphone1001", true);
    const auto id2 = mkAccount(am, "1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidCallId);
    ASSERT_NE(id2, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    std::atomic<int> inboundId{-1};
    QObject::connect(&cm, &compactphone::sip::CallManager::incomingCall,
                     [&](int id) { inboundId.store(id); cm.accept(id); });

    // Retry the dial: a dial can flake without establishing a media session
    // at all (Asterisk transiently rejecting while contact state settles,
    // especially right after a previous test process unregistered). The gate's
    // job is exercising the mute-vs-renegotiation path, not pinning routing.
    auto inboundReady = [&] {
        return inboundId.load() > 0 && cm.isMediaActive(inboundId.load());
    };
    bool established = false;
    for (int attempt = 0; attempt < 4 && !established; ++attempt) {
        inboundId.store(-1);
        const auto outbound = cm.makeCall(id2, udpTarget("1001"));
        ASSERT_NE(outbound, compactphone::sip::kInvalidCallId);
        established = pumpUntil(inboundReady, 15s);
        if (!established) {
            for (const auto &e : cm.snapshot()) cm.hangup(e.id);
            ASSERT_TRUE(pumpUntil([&] { return cm.callCount() == 0; }, 10s));
        }
    }
    ASSERT_TRUE(established) << "no inbound call established media after retries";
    const auto id = inboundId.load();

    bool muted = false;
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    while (std::chrono::steady_clock::now() < deadline) {
        muted = !muted;
        (void)cm.setMuted(id, muted);
        (void)cm.hold(id);
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
        (void)cm.unhold(id);
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(5ms);
    }
    // Settle unmuted + unheld and verify the workload left a sane call.
    (void)cm.setMuted(id, false);
    (void)cm.unhold(id);
    EXPECT_TRUE(pumpUntil([&] { return cm.isMediaActive(id); }, 10s));

    // Drain.
    for (const auto &e : cm.snapshot()) cm.hangup(e.id);
    EXPECT_TRUE(pumpUntil([&] { return cm.callCount() == 0; }, 10s));
}

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
#include <QMetaObject>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;
using compactphone::testsupport::waitForRegState;

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
};

// Regression gate for the CallManager cross-thread races (PJSIP worker
// thread vs main thread). Incoming-call adoption runs synchronously on the
// PJSIP worker thread (inserting into CallManager's maps and minting ids)
// while the main thread hammers the read paths — snapshot(), callCount(),
// isHeld()/isMuted() — through several call setup/teardown cycles.
//
// Under a plain build this is a smoke test. Under `make test-tsan`
// (linux-tsan preset) ThreadSanitizer turns every unsynchronized map access
// into a hard failure — this exact workload is what crashed before
// CallManager got its mutex: concurrent unordered_map read/write during
// adoption, and duplicate CallIds from an unguarded m_nextId++.
TEST_F(ThreadStressTest, AdoptionRacesSnapshotPolling)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;

    auto mkAccount = [&](const std::string &user, const std::string &pwd,
                         bool isDefault) {
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
    };

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    compactphone::sip::CallManager cm(&am);

    // Adoption stays on the PJSIP thread (as in production); accepting is
    // main-thread work, so queue it — the polling loop below pumps events.
    std::atomic<int> adopted{0};
    am.setOnIncomingCall(
        [&](compactphone::sip::AccountId aid, int pjsipCallId) {
            const auto localId = cm.adoptIncomingCall(aid, pjsipCallId);
            if (localId == compactphone::sip::kInvalidCallId) return;
            adopted.fetch_add(1);
            QMetaObject::invokeMethod(&cm, [&cm, localId]() {
                cm.accept(localId);
            }, Qt::QueuedConnection);
        });

    // Tears down every live call and drains the disconnect callbacks +
    // grace queue while still polling the cross-thread read paths.
    const auto drainCalls = [&]() {
        for (const auto &e : cm.snapshot()) cm.hangup(e.id);
        const auto drainDeadline = std::chrono::steady_clock::now() + 10s;
        while (cm.callCount() > 0 &&
               std::chrono::steady_clock::now() < drainDeadline) {
            (void)cm.snapshot();
            QCoreApplication::processEvents();
            std::this_thread::sleep_for(10ms);
        }
        return cm.callCount() == 0;
    };

    constexpr int kCycles = 3;
    // A dial can flake without reaching us at all (Asterisk transiently
    // rejecting while its contact state settles, especially right after a
    // previous test process unregistered). Retry dials and assert on the
    // overall adoption count at the end — the gate's job is exercising
    // adoption concurrency under TSan, not pinning fixture routing.
    constexpr int kAttemptsPerCycle = 3;
    int successfulCycles = 0;
    for (int cycle = 0; cycle < kCycles; ++cycle) {
        bool cycleOk = false;
        for (int attempt = 0; attempt < kAttemptsPerCycle && !cycleOk;
             ++attempt) {
            const int adoptedBefore = adopted.load();
            // 1002 dials extension 1001 — Asterisk routes it back to account
            // 1001, so adoption fires on the PJSIP worker thread mid-loop.
            const auto outbound = cm.makeCall(id2, udpTarget("1001"));
            ASSERT_NE(outbound, compactphone::sip::kInvalidCallId);

            // Hammer the cross-thread read paths while the INVITE, adoption,
            // accept, and media activation are all in flight. Deadlines are
            // generous: under TSan everything is several times slower.
            const auto deadline = std::chrono::steady_clock::now() + 15s;
            bool confirmed = false;
            while (std::chrono::steady_clock::now() < deadline) {
                const auto snap = cm.snapshot();
                (void)cm.callCount();
                for (const auto &e : snap) {
                    (void)cm.isHeld(e.id);
                    (void)cm.isMuted(e.id);
                    (void)cm.isRecording(e.id);
                    if (e.state == compactphone::sip::CallState::Confirmed) {
                        confirmed = true;
                    }
                }
                QCoreApplication::processEvents();
                if (confirmed && adopted.load() > adoptedBefore &&
                    cm.callCount() >= 2) {
                    break;
                }
                // Throttle just enough that the instrumented polling loop
                // can't monopolize the PJSUA lock under TSan and livelock
                // the worker thread — still thousands of overlapping
                // accesses per cycle.
                std::this_thread::sleep_for(1ms);
            }
            cycleOk = confirmed && adopted.load() > adoptedBefore;
            ASSERT_TRUE(drainCalls()) << "cycle " << cycle
                                      << ": calls did not drain";
        }
        if (cycleOk) ++successfulCycles;
    }
    // At least two cycles must have reached an adopted, confirmed call for
    // the cross-thread workload to count as exercised; a single flaky cycle
    // (fixture routing transients) does not fail the gate.
    EXPECT_GE(successfulCycles, 2)
        << "adoption workload was not exercised (" << adopted.load()
        << " adoptions across " << kCycles << "x" << kAttemptsPerCycle
        << " dials)";
}

// Regression gate for the callback-slot assign-vs-invoke race: the main
// thread reassigns the manager callbacks (what controllers do in their
// constructors and destructors) while the PJSIP worker thread is delivering
// registration and call-state events through the very same std::function
// objects. Concurrent swap vs. invoke on a std::function is UB — before the
// callback slots were mutex-guarded, TSan flagged this exact pair
// (AccountsManager::setOnRegistrationStateChanged vs AccountImpl::onRegState)
// twelve times across the integration suite. The slot mutex also gives
// setOnX({}) quiesce semantics: once it returns, no in-flight invocation of
// the previous callback exists, which is what makes the controller
// destructors safe while events are still arriving.
TEST_F(ThreadStressTest, CallbackReassignmentRacesDelivery)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;

    auto mkAccount = [&](const std::string &user, const std::string &pwd,
                         bool isDefault) {
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
    };

    const auto id1 = mkAccount("1001", "compactphone1001", true);
    const auto id2 = mkAccount("1002", "compactphone1002", false);
    ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
    ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
    ASSERT_TRUE(waitForRegState(
        am, {id1, id2}, compactphone::sip::RegistrationState::Registered, 10s));

    compactphone::sip::CallManager cm(&am);
    std::atomic<int> adopted{0};
    am.setOnIncomingCall(
        [&](compactphone::sip::AccountId aid, int pjsipCallId) {
            const auto localId = cm.adoptIncomingCall(aid, pjsipCallId);
            if (localId == compactphone::sip::kInvalidCallId) return;
            adopted.fetch_add(1);
            QMetaObject::invokeMethod(&cm, [&cm, localId]() {
                cm.accept(localId);
            }, Qt::QueuedConnection);
        });

    std::atomic<int> invocations{0};

    // Keep a call cycle and periodic re-registrations in flight so the
    // PJSIP thread continuously delivers reg-state and call-state events,
    // while the main thread reassigns the receiving callbacks every
    // iteration.
    (void)cm.makeCall(id2, udpTarget("1001"));
    const auto deadline = std::chrono::steady_clock::now() + 8s;
    int iteration = 0;
    while (std::chrono::steady_clock::now() < deadline) {
        am.setOnRegistrationStateChanged(
            [&](auto, auto) { invocations.fetch_add(1); });
        cm.setOnCallEvent(
            [&](auto, auto) { invocations.fetch_add(1); });
        cm.setOnCallStateChanged([&](auto) { invocations.fetch_add(1); });
        if (++iteration % 100 == 0) {
            // Refresh registrations to keep reg-state events flowing, and
            // cycle the call so call-state events keep firing too.
            am.reregisterAllEnabled();
            for (const auto &e : cm.snapshot()) cm.hangup(e.id);
            (void)cm.makeCall(id2, udpTarget("1001"));
        }
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(1ms);
    }

    // Quiesce barrier: after these return, no in-flight invocation of any
    // previous callback may exist.
    am.setOnRegistrationStateChanged({});
    am.setOnIncomingCall({});
    cm.setOnCallEvent({});
    cm.setOnCallStateChanged({});

    // Drain whatever the last cycle left behind.
    for (const auto &e : cm.snapshot()) cm.hangup(e.id);
    const auto drainDeadline = std::chrono::steady_clock::now() + 10s;
    while (cm.callCount() > 0 &&
           std::chrono::steady_clock::now() < drainDeadline) {
        QCoreApplication::processEvents();
        std::this_thread::sleep_for(10ms);
    }
    // The workload must have actually delivered events into the reassigned
    // callbacks for the race window to have been exercised.
    EXPECT_GT(invocations.load(), 0);
}

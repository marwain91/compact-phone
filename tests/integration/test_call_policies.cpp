#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallsController.h"
#include "core/CallManager.h"
#include "core/HistoryManager.h"
#include "core/SettingsController.h"
#include "core/SettingsManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "models/CallsModel.h"
#include "models/HistoryModel.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>
#include <unordered_map>

using namespace std::chrono_literals;

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

// CallsController fans state changes through QMetaObject::invokeMethod
// with QueuedConnection, so the policies (DND / auto-answer / forward)
// only fire while the Qt event loop is running. Plain cv.wait_for would
// block the loop; this helper polls the predicate while pumping events.
template <typename Pred>
bool pumpUntil(Pred pred, std::chrono::milliseconds timeout)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
        if (pred()) return true;
        std::this_thread::sleep_for(20ms);
    }
    return pred();
}
} // namespace

// Controller-level integration tests: DND must short-circuit incoming
// calls with a 486-style decline, and auto-answer must accept them
// without a manual call.
class CallPoliciesTest : public ::testing::Test {
protected:
    int argc = 1;
    char argv0[1] = {0};
    char *argv = argv0;
    std::unique_ptr<QCoreApplication> app;

    compactphone::sip::SipEngine engine;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;
    std::unique_ptr<compactphone::sip::AccountsManager> am;
    std::unique_ptr<compactphone::sip::CallManager> cm;
    std::unique_ptr<compactphone::models::CallsModel> callsModel;
    std::unique_ptr<compactphone::sip::HistoryManager> hm;
    std::unique_ptr<compactphone::models::HistoryModel> historyModel;
    std::unique_ptr<compactphone::sip::SettingsManager> sm;
    std::unique_ptr<compactphone::SettingsController> sc;
    std::unique_ptr<compactphone::CallsController> cc;

    // Stays accessible to the lambdas wired into CallsController.
    int activeAccountId = -1;

    void SetUp() override
    {
        app = std::make_unique<QCoreApplication>(argc, &argv);
        ASSERT_TRUE(engine.start(0));
        ASSERT_TRUE(db.openInMemory());

        am = std::make_unique<compactphone::sip::AccountsManager>(&engine, &db, &kc);
        cm = std::make_unique<compactphone::sip::CallManager>(am.get());
        callsModel = std::make_unique<compactphone::models::CallsModel>(cm.get());
        hm = std::make_unique<compactphone::sip::HistoryManager>(&db);
        historyModel = std::make_unique<compactphone::models::HistoryModel>(hm.get());
        sm = std::make_unique<compactphone::sip::SettingsManager>(&db);
        sc = std::make_unique<compactphone::SettingsController>(
            &engine, sm.get(), QStringLiteral("/tmp"));
        cc = std::make_unique<compactphone::CallsController>(
            am.get(), cm.get(), callsModel.get(),
            hm.get(), historyModel.get(),
            sc.get(),
            [this] { return activeAccountId; },
            [](const QString &, int) {});
    }
    void TearDown() override
    {
        cc.reset();
        sc.reset();
        sm.reset();
        historyModel.reset();
        hm.reset();
        callsModel.reset();
        cm.reset();
        am.reset();
        engine.stop();
    }

    // Register both 1001 and 1002 and block until both are registered.
    void registerBothAccounts(compactphone::sip::AccountId &id1,
                              compactphone::sip::AccountId &id2)
    {
        std::mutex mtx;
        std::condition_variable cv;
        int registeredCount = 0;
        am->setOnRegistrationStateChanged([&](auto, auto s) {
            std::lock_guard l(mtx);
            if (s == compactphone::sip::RegistrationState::Registered) ++registeredCount;
            cv.notify_all();
        });
        auto mk = [&](const std::string &u, const std::string &p, bool def) {
            compactphone::sip::Account a;
            a.displayName = u; a.username = u; a.domain = sipServer();
            a.authUser = u; a.transport = compactphone::sip::Transport::Udp;
            a.enabled = true; a.isDefault = def; a.registerOnStartup = true;
            return am->add(a, p);
        };
        id1 = mk("1001", "compactphone1001", true);
        id2 = mk("1002", "compactphone1002", false);
        ASSERT_NE(id1, compactphone::sip::kInvalidAccountId);
        ASSERT_NE(id2, compactphone::sip::kInvalidAccountId);
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] { return registeredCount >= 2; }));
    }
};

TEST_F(CallPoliciesTest, DndDeclinesIncomingCallAndOriginatorDisconnects)
{
    compactphone::sip::AccountId id1, id2;
    registerBothAccounts(id1, id2);
    activeAccountId = id1;

    sc->setDndEnabled(true);

    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    cm->setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed[id] = s;
    });

    // 1001 dials 1002 — Asterisk routes the INVITE to the registered
    // 1002 endpoint (our own pjsua), where the controller's DND policy
    // declines the dialog before the user ever sees the incoming card.
    auto callerLeg = cm->makeCall(id1, udpTarget("1002"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);

    EXPECT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Disconnected;
    }, 10s)) << "caller leg never went Disconnected";
}

TEST_F(CallPoliciesTest, AutoAnswerAcceptsIncomingCallWithoutManualAccept)
{
    compactphone::sip::AccountId id1, id2;
    registerBothAccounts(id1, id2);
    activeAccountId = id1;

    sc->setAutoAnswerEnabled(true);
    sc->setAutoAnswerDelayMs(0);

    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    cm->setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed[id] = s;
    });

    // 1001 -> 1002. With auto-answer on (delay 0), the controller calls
    // accept() the moment it sees the incoming dialog. Caller observes
    // 200 OK -> Confirmed.
    auto callerLeg = cm->makeCall(id1, udpTarget("1002"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);

    EXPECT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Confirmed;
    }, 15s)) << "caller leg never reached Confirmed under auto-answer";

    cm->hangup(callerLeg);
    pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Disconnected;
    }, 5s);
}

TEST_F(CallPoliciesTest, AutoAnswerWithDelayWaitsThenAccepts)
{
    compactphone::sip::AccountId id1, id2;
    registerBothAccounts(id1, id2);
    activeAccountId = id1;

    sc->setAutoAnswerEnabled(true);
    sc->setAutoAnswerDelayMs(1500);

    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    auto firstSeenConfirmed = std::chrono::steady_clock::time_point{};
    cm->setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx);
        observed[id] = s;
        if (s == compactphone::sip::CallState::Confirmed &&
            firstSeenConfirmed.time_since_epoch().count() == 0) {
            firstSeenConfirmed = std::chrono::steady_clock::now();
        }
    });

    const auto inviteSent = std::chrono::steady_clock::now();
    auto callerLeg = cm->makeCall(id1, udpTarget("1002"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);

    EXPECT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Confirmed;
    }, 15s));

    const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        firstSeenConfirmed - inviteSent).count();
    EXPECT_GE(elapsedMs, 1000) << "auto-answer fired faster than the delay";

    cm->hangup(callerLeg);
    pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Disconnected;
    }, 5s);
}

TEST_F(CallPoliciesTest, ForwardAlwaysRedirectsIncoming)
{
    compactphone::sip::AccountId id1, id2;
    registerBothAccounts(id1, id2);
    activeAccountId = id1;

    sc->setCfwdAlwaysEnabled(true);
    sc->setCfwdAlwaysTarget(QString::fromStdString(udpTarget("600")));

    std::mutex mtx;
    std::unordered_map<int, compactphone::sip::CallState> observed;
    cm->setOnCallEvent([&](compactphone::sip::CallId id, compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed[id] = s;
    });

    // 1001 -> 1002. Controller sees the incoming dialog and immediately
    // sends 302 Contact: <600>. Caller may either get redirected by
    // Asterisk (-> Confirmed via echo) or torn down (-> Disconnected),
    // depending on Asterisk's redirect handling — either outcome proves
    // the 302 fired. We just assert it reaches *some* terminal call
    // state within 15s, and never gets stuck in Calling forever.
    auto callerLeg = cm->makeCall(id1, udpTarget("1002"));
    ASSERT_NE(callerLeg, compactphone::sip::kInvalidCallId);

    EXPECT_TRUE(pumpUntil([&] {
        std::lock_guard l(mtx);
        const auto s = observed[callerLeg];
        return s == compactphone::sip::CallState::Disconnected
            || s == compactphone::sip::CallState::Confirmed;
    }, 15s)) << "caller leg never reached a terminal state under forward-always";

    cm->hangup(callerLeg);
    pumpUntil([&] {
        std::lock_guard l(mtx);
        return observed[callerLeg] == compactphone::sip::CallState::Disconnected;
    }, 5s);
}

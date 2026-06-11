#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallEntry.h"
#include "core/CallManager.h"
#include "core/platform/Keychain_memory.h"
#include "core/sipbackend/ListenerFanout.h"
#include "core/sipbackend/fake/FakeSipBackend.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <algorithm>
#include <chrono>
#include <memory>
#include <string>
#include <thread>

using namespace std::chrono_literals;
namespace sb = compactphone::sipbackend;
namespace sip = compactphone::sip;

namespace {

QCoreApplication *ensureApp()
{
    static int argc = 1;
    static char arg0[] = "test_call_manager";
    static char *argv[] = {arg0, nullptr};
    static QCoreApplication *app = QCoreApplication::instance()
        ? QCoreApplication::instance()
        : new QCoreApplication(argc, argv);
    return app;
}

template <typename Pred>
bool pump(Pred pred, std::chrono::milliseconds timeout = 2000ms)
{
    const auto deadline = std::chrono::steady_clock::now() + timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        if (pred()) return true;
        std::this_thread::sleep_for(2ms);
    }
    return pred();
}

bool logContains(const sb::FakeSipBackend &fake, const std::string &needle)
{
    const auto &log = fake.commandLog();
    return std::any_of(log.begin(), log.end(), [&](const std::string &line) {
        return line == needle;
    });
}

} // namespace

// Production-shaped wiring: fake backend -> ListenerFanout(accounts, calls),
// one registered account. Mirrors buildCoreSipGraph order.
class CallManagerFakeTest : public ::testing::Test {
protected:
    sb::FakeSipBackend fake;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;
    std::unique_ptr<sip::AccountsManager> am;
    std::unique_ptr<sip::CallManager> cm;
    std::unique_ptr<sb::ListenerFanout> fanout;

    sip::AccountId accountId = sip::kInvalidAccountId;
    sb::AccountId backendAccId = sb::kInvalidAccountId;

    void SetUp() override
    {
        ensureApp();
        ASSERT_TRUE(db.openInMemory());
        ASSERT_TRUE(fake.start(sb::EngineConfig{}));
        am = std::make_unique<sip::AccountsManager>(&fake, &db, &kc);
        cm = std::make_unique<sip::CallManager>(&fake, am.get());
        cm->setLingerMsForTest(30);
        fanout = std::make_unique<sb::ListenerFanout>(
            std::vector<sb::ISipBackendListener *>{am.get(), cm.get()});
        fake.setListener(fanout.get());

        sip::Account a;
        a.displayName = "Unit";
        a.username = "100";
        a.domain = "unit.test";
        a.enabled = true;
        a.isDefault = true;
        a.registerOnStartup = true;
        accountId = am->add(a, "pw");
        ASSERT_NE(accountId, sip::kInvalidAccountId);
        backendAccId = fake.lastAddedAccountId();
        ASSERT_NE(backendAccId, sb::kInvalidAccountId);
    }

    void TearDown() override
    {
        fake.setListener(nullptr);
        cm.reset();
        am.reset();
    }

    // Places an outbound call and pumps it to Confirmed.
    sip::CallId confirmedOutbound(const std::string &uri = "sip:200@unit.test")
    {
        const auto id = cm->makeCall(accountId, uri);
        if (id == sip::kInvalidCallId) return id;
        fake.simulateRemoteAnswer(id);
        pump([&] { return cm->snapshot().size() >= 1
                       && cm->isMediaActive(id); });
        return id;
    }

    // Simulates an incoming call, pumps until CallManager announces it,
    // and returns the id delivered through the incomingCall signal.
    sip::CallId pumpedIncoming(const std::string &uri = "sip:300@unit.test",
                               const std::string &display = "Alice")
    {
        int signalled = -1;
        const auto conn = QObject::connect(
            cm.get(), &sip::CallManager::incomingCall,
            [&signalled](int id) { signalled = id; });
        const auto backendCallId =
            fake.simulateIncomingCall(backendAccId, uri, display);
        EXPECT_NE(backendCallId, sb::kInvalidCallId);
        pump([&] { return signalled != -1; });
        QObject::disconnect(conn);
        EXPECT_EQ(signalled, backendCallId);  // id-space unification
        return static_cast<sip::CallId>(signalled);
    }
};

TEST_F(CallManagerFakeTest, IncomingCallCreatesInboundRecordWithPushedIdentity)
{
    const auto id = pumpedIncoming("sip:300@unit.test", "Alice");
    ASSERT_NE(id, sip::kInvalidCallId);
    const auto snap = cm->snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].id, id);
    EXPECT_EQ(snap[0].remoteUri, "sip:300@unit.test");
    EXPECT_EQ(snap[0].remoteDisplayName, "Alice");
    EXPECT_EQ(snap[0].state, sip::CallState::Calling);
    EXPECT_EQ(snap[0].direction, sip::CallDirection::Inbound);
}

TEST_F(CallManagerFakeTest, DeclineSendsBusyHereThroughBackend)
{
    const auto id = pumpedIncoming();
    ASSERT_TRUE(cm->decline(id));
    EXPECT_TRUE(logContains(fake, "decline:" + std::to_string(id) + ":486"));
    // The Disconnected event carries the code; readable immediately after.
    ASSERT_TRUE(pump([&] { return cm->lastStatusCode(id) == 486; }));
}

TEST_F(CallManagerFakeTest, AcceptPromotesAndAutoHoldsPreviousActive)
{
    const auto outbound = confirmedOutbound();
    ASSERT_NE(outbound, sip::kInvalidCallId);
    EXPECT_EQ(cm->activeCallId(), outbound);

    const auto inbound = pumpedIncoming();
    ASSERT_TRUE(cm->accept(inbound));
    EXPECT_TRUE(logContains(fake, "hold:" + std::to_string(outbound)));
    EXPECT_TRUE(logContains(fake, "answer:" + std::to_string(inbound)));
    EXPECT_EQ(cm->activeCallId(), inbound);
    EXPECT_TRUE(cm->isHeld(outbound));
}

TEST_F(CallManagerFakeTest, UnholdPromotesAndHoldsPreviousActive)
{
    const auto first = confirmedOutbound("sip:201@unit.test");
    const auto second = pumpedIncoming();
    ASSERT_TRUE(cm->accept(second));
    pump([&] { return cm->isMediaActive(second); });
    ASSERT_TRUE(cm->isHeld(first));

    ASSERT_TRUE(cm->unhold(first));
    EXPECT_TRUE(logContains(fake, "unhold:" + std::to_string(first)));
    EXPECT_TRUE(logContains(fake, "hold:" + std::to_string(second)));
    EXPECT_EQ(cm->activeCallId(), first);
    EXPECT_TRUE(cm->isHeld(second));
    EXPECT_FALSE(cm->isHeld(first));
}

TEST_F(CallManagerFakeTest, BlindTransferFinal2xxHangsUpOriginatingLeg)
{
    const auto id = confirmedOutbound();
    ASSERT_TRUE(cm->blindTransfer(id, "sip:400@unit.test"));
    EXPECT_TRUE(logContains(
        fake, "blindTransfer:" + std::to_string(id) + ":sip:400@unit.test"));
    fake.simulateTransferStatus(id, 200, true, "OK");
    ASSERT_TRUE(pump([&] {
        return logContains(fake, "hangup:" + std::to_string(id));
    }));
}

TEST_F(CallManagerFakeTest, BlindTransferFailureKeepsTheCall)
{
    const auto id = confirmedOutbound();
    ASSERT_TRUE(cm->blindTransfer(id, "sip:400@unit.test"));
    fake.simulateTransferStatus(id, 486, true, "Busy Here");
    pump([] { return false; }, 100ms);   // let any (wrong) hangup surface
    EXPECT_FALSE(logContains(fake, "hangup:" + std::to_string(id)));
    EXPECT_EQ(cm->callCount(), 1u);
    EXPECT_TRUE(cm->isMediaActive(id));
}

TEST_F(CallManagerFakeTest, AttendedTransferFinal2xxHangsUpBothLegs)
{
    const auto active = confirmedOutbound("sip:201@unit.test");
    const auto dest = pumpedIncoming();
    ASSERT_TRUE(cm->accept(dest));
    pump([&] { return cm->isMediaActive(dest); });

    ASSERT_TRUE(cm->attendedTransfer(dest, active));
    fake.simulateTransferStatus(dest, 200, true, "OK");
    ASSERT_TRUE(pump([&] {
        return logContains(fake, "hangup:" + std::to_string(dest))
            && logContains(fake, "hangup:" + std::to_string(active));
    }));
}

TEST_F(CallManagerFakeTest, DisconnectMovesRecordToLingerThenErases)
{
    const auto id = confirmedOutbound();
    fake.simulateRemoteHangup(id, 200);
    // Live record gone, lingering snapshot still visible, backend released.
    ASSERT_TRUE(pump([&] { return cm->callCount() == 0; }));
    auto snap = cm->snapshot();
    ASSERT_EQ(snap.size(), 1u);
    EXPECT_EQ(snap[0].state, sip::CallState::Disconnected);
    EXPECT_EQ(snap[0].remoteUri, "sip:200@unit.test");
    EXPECT_TRUE(logContains(fake, "releaseCall:" + std::to_string(id)));
    // After the (shortened) linger window the card disappears.
    ASSERT_TRUE(pump([&] { return cm->snapshot().empty(); }));
}

TEST_F(CallManagerFakeTest, LastStatusCodeReadableAfterRemoteDecline)
{
    const auto id = cm->makeCall(accountId, "sip:200@unit.test");
    ASSERT_NE(id, sip::kInvalidCallId);
    fake.simulateRemoteHangup(id, 486);
    ASSERT_TRUE(pump([&] { return cm->lastStatusCode(id) == 486; }));
}

TEST_F(CallManagerFakeTest, MuteRoundTripsThroughBackend)
{
    const auto id = confirmedOutbound();
    ASSERT_TRUE(cm->setMuted(id, true));
    EXPECT_TRUE(logContains(fake, "setMuted:" + std::to_string(id) + ":1"));
    EXPECT_TRUE(cm->isMuted(id));
    EXPECT_FALSE(cm->isCaptureTransmitting(id));
    ASSERT_TRUE(cm->setMuted(id, false));
    EXPECT_FALSE(cm->isMuted(id));
    EXPECT_TRUE(cm->isCaptureTransmitting(id));
}

TEST_F(CallManagerFakeTest, IncomingForRemovedAccountIsDeclined480)
{
    int signalled = -1;
    QObject::connect(cm.get(), &sip::CallManager::incomingCall,
                     [&signalled](int id) { signalled = id; });
    const auto backendCallId =
        fake.simulateIncomingCall(backendAccId, "sip:300@unit.test", "");
    ASSERT_NE(backendCallId, sb::kInvalidCallId);
    // Remove the domain account before the queued event is delivered.
    ASSERT_TRUE(am->remove(accountId));
    pump([] { return false; }, 100ms);
    EXPECT_EQ(signalled, -1);
    EXPECT_TRUE(logContains(
        fake, "decline:" + std::to_string(backendCallId) + ":480"));
    EXPECT_EQ(cm->callCount(), 0u);
}

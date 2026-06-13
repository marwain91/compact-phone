#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdlib>

using namespace std::chrono_literals;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class StreamStatsLiveTest : public ::testing::Test {
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

// Drive real RTP both ways (the echo extension reflects our media) and prove
// CallManager::streamStats actually samples the live stream. Only the pure MOS
// parser was covered before; the getStreamStat path had no integration test.
// jitterMs is derived from the rx stats and populates as RTP arrives, so it is
// the fastest reliable signal that sampling works; a regression that stopped
// sampling (or wired the wrong RTCP field) leaves it at the -1 sentinel.
TEST_F(StreamStatsLiveTest, SamplesRtpStreamForActiveCall)
{
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "Stats";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.isDefault = true;
    a.registerOnStartup = true;
    const auto accId = am.add(a, "compactphone1001");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        am, {accId}, compactphone::sip::RegistrationState::Registered, 10s));

    auto &cm = smp.calls;
    QObject::connect(&cm, &compactphone::sip::CallManager::callStateChanged, [&](compactphone::sip::CallState s) {
        observed.store(s);
    });

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Confirmed;
    }, 15s));

    // Let RTP flow, polling until the jitter stat moves off the -1 sentinel.
    // Generous deadline: under the TSan gate the instrumented media path can
    // take several times longer to surface the first RTCP-derived sample
    // (one burn-in run blew a 12s budget while the suite ran under load).
    const bool sampled = pumpUntil([&] {
        return cm.streamStats(callId).jitterMs >= 0;
    }, 45s);
    const auto s = cm.streamStats(callId);
    EXPECT_TRUE(sampled) << "jitterMs stayed at sentinel (" << s.jitterMs << ")";
    EXPECT_GE(s.jitterMs, 0);

    cm.hangup(callId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

// An unknown call id yields the default sentinel struct (all -1), not garbage.
TEST_F(StreamStatsLiveTest, UnknownCallReturnsSentinelStats)
{
    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    auto &cm = smp.calls;
    const auto s = cm.streamStats(9999);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
}

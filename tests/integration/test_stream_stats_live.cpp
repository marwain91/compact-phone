#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <QCoreApplication>

#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <mutex>
#include <thread>

using namespace std::chrono_literals;

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
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Stats";
    a.username = "1001";
    a.domain = sipServer();
    a.authUser = "1001";
    a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true;
    a.isDefault = true;
    a.registerOnStartup = true;
    ASSERT_NE(am.add(a, "compactphone1001"), compactphone::sip::kInvalidAccountId);

    std::mutex mtx;
    std::condition_variable cv;
    compactphone::sip::RegistrationState rstate =
        compactphone::sip::RegistrationState::Unregistered;
    am.setOnRegistrationStateChanged([&](auto, auto s) {
        std::lock_guard l(mtx); rstate = s; cv.notify_all();
    });
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 10s, [&] {
            return rstate == compactphone::sip::RegistrationState::Registered;
        }));
    }

    compactphone::sip::CallManager cm(&am);
    compactphone::sip::CallState observed = compactphone::sip::CallState::Idle;
    cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
        std::lock_guard l(mtx); observed = s; cv.notify_all();
    });

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 15s, [&] {
            return observed == compactphone::sip::CallState::Confirmed;
        }));
    }

    // Let RTP flow, polling until the jitter stat moves off the -1 sentinel.
    bool sampled = false;
    for (int i = 0; i < 120 && !sampled; ++i) {
        if (cm.streamStats(callId).jitterMs >= 0) sampled = true;
        else std::this_thread::sleep_for(100ms);
    }
    const auto s = cm.streamStats(callId);
    EXPECT_TRUE(sampled) << "jitterMs stayed at sentinel (" << s.jitterMs << ")";
    EXPECT_GE(s.jitterMs, 0);

    cm.hangup(callId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
}

// An unknown call id yields the default sentinel struct (all -1), not garbage.
TEST_F(StreamStatsLiveTest, UnknownCallReturnsSentinelStats)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::CallManager cm(&am);
    const auto s = cm.streamStats(9999);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
}

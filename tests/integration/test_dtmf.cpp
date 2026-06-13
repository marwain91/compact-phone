#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/sipbackend/pjsip/SipEngine.h"
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

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class DtmfTest : public ::testing::Test {
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

TEST_F(DtmfTest, SendsRfc2833Digits)
{
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "D"; a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    a.dtmfMethod = compactphone::sip::DtmfMethod::Rfc2833;
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

    std::this_thread::sleep_for(1s);
    EXPECT_TRUE(cm.sendDtmf(callId, "1234"));
    std::this_thread::sleep_for(1s);

    cm.hangup(callId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

TEST_F(DtmfTest, SendsSipInfoDigits)
{
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "D-Info"; a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    a.dtmfMethod = compactphone::sip::DtmfMethod::Info;
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

    std::this_thread::sleep_for(1s);
    EXPECT_TRUE(cm.sendDtmf(callId, "90#"));
    std::this_thread::sleep_for(1s);

    cm.hangup(callId);
    ASSERT_TRUE(pumpUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

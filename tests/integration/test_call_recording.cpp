#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include "test_support.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <thread>

using namespace std::chrono_literals;
using compactphone::testsupport::pollUntil;
using compactphone::testsupport::pumpUntil;
using compactphone::testsupport::waitForRegState;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}
} // namespace

class CallRecordingTest : public ::testing::Test {
protected:
    int argc = 1;
    char argv0[1] = {0};
    char *argv = argv0;
    std::unique_ptr<QCoreApplication> app;

    compactphone::sip::SipEngine engine;
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    QString recordingPath;

    void SetUp() override
    {
        app = std::make_unique<QCoreApplication>(argc, &argv);
        ASSERT_TRUE(engine.start(0));
        ASSERT_TRUE(db.openInMemory());

        const QString dir = QDir::tempPath()
            + QStringLiteral("/compactphone-test-recordings");
        QDir().mkpath(dir);
        recordingPath = dir + QStringLiteral("/test_call.wav");
        QFile::remove(recordingPath);
    }
    void TearDown() override {
        engine.stop();
        QFile::remove(recordingPath);
    }
};

TEST_F(CallRecordingTest, RecordsActiveCallToWavFile)
{
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "Rec";
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

    compactphone::sip::CallManager cm(&am);
    cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
        observed.store(s);
    });

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pollUntil([&] {
        return observed.load() == compactphone::sip::CallState::Confirmed;
    }, 15s));

    // PJSIP's media state can lag the Confirmed signalling state by a
    // round-trip. Give it a moment so firstActiveAudio resolves cleanly.
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.startRecording(callId, recordingPath.toStdString()));
    EXPECT_TRUE(cm.isRecording(callId));

    // Let real audio flow into the recorder for a couple of seconds — the
    // echo prompt is plenty. Then stop recording explicitly (not via
    // hangup) so we can assert the file contents while the call is alive.
    std::this_thread::sleep_for(2500ms);

    EXPECT_TRUE(cm.stopRecording(callId));
    EXPECT_FALSE(cm.isRecording(callId));

    QFileInfo fi(recordingPath);
    EXPECT_TRUE(fi.exists()) << "WAV file was not created";
    // A WAV with > 1s of 16kHz/16-bit/mono audio is ~32kB. We just guard
    // against a header-only file by requiring >= 8kB (which is ~0.25s).
    EXPECT_GE(fi.size(), 8 * 1024)
        << "Recording file is suspiciously small (" << fi.size() << " bytes)";

    cm.hangup(callId);
    ASSERT_TRUE(pollUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
}

TEST_F(CallRecordingTest, EraseCallCleansUpActiveRecorder)
{
    // Record gets implicitly stopped when the call ends (eraseCall ->
    // recorder destructor flushes the WAV). Verify the file is closed
    // (size > 0) and isRecording returns false even though stopRecording
    // was never called explicitly.
    std::atomic<compactphone::sip::CallState> observed{
        compactphone::sip::CallState::Idle};

    compactphone::testsupport::SipManagerPair smp(&engine, &db, &kc);
    auto &am = smp.manager;
    compactphone::sip::Account a;
    a.displayName = "Rec";
    a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
    const auto accId = am.add(a, "compactphone1001");
    ASSERT_NE(accId, compactphone::sip::kInvalidAccountId);

    ASSERT_TRUE(waitForRegState(
        am, {accId}, compactphone::sip::RegistrationState::Registered, 10s));

    compactphone::sip::CallManager cm(&am);
    cm.setOnCallStateChanged([&](compactphone::sip::CallState s) {
        observed.store(s);
    });

    auto callId = cm.makeCall("sip:600@" + sipServer());
    ASSERT_NE(callId, compactphone::sip::kInvalidCallId);
    ASSERT_TRUE(pollUntil([&] {
        return observed.load() == compactphone::sip::CallState::Confirmed;
    }, 15s));
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.startRecording(callId, recordingPath.toStdString()));
    std::this_thread::sleep_for(1500ms);

    cm.hangup(callId);
    ASSERT_TRUE(pollUntil([&] {
        return observed.load() == compactphone::sip::CallState::Disconnected;
    }, 5s));
    // Pump the event loop so the 2.2s grace timer fires eraseCall.
    pumpUntil([&] { return cm.callCount() == 0; }, 8s);

    EXPECT_FALSE(cm.isRecording(callId));
    QFileInfo fi(recordingPath);
    EXPECT_TRUE(fi.exists());
    EXPECT_GT(fi.size(), 0);
}

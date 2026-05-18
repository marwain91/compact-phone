#include <gtest/gtest.h>

#include "core/Account.h"
#include "core/AccountsManager.h"
#include "core/CallManager.h"
#include "core/SipEngine.h"
#include "core/platform/Keychain_memory.h"
#include "persistence/Database.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

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
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Rec";
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
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
}

TEST_F(CallRecordingTest, EraseCallCleansUpActiveRecorder)
{
    // Record gets implicitly stopped when the call ends (eraseCall ->
    // recorder destructor flushes the WAV). Verify the file is closed
    // (size > 0) and isRecording returns false even though stopRecording
    // was never called explicitly.
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Rec";
    a.username = "1001"; a.domain = sipServer();
    a.authUser = "1001"; a.transport = compactphone::sip::Transport::Udp;
    a.enabled = true; a.isDefault = true; a.registerOnStartup = true;
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
    std::this_thread::sleep_for(500ms);

    EXPECT_TRUE(cm.startRecording(callId, recordingPath.toStdString()));
    std::this_thread::sleep_for(1500ms);

    cm.hangup(callId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
    // Pump event loop so the 2.2s grace timer fires eraseCall.
    for (int i = 0; i < 150; ++i) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        std::this_thread::sleep_for(20ms);
    }

    EXPECT_FALSE(cm.isRecording(callId));
    QFileInfo fi(recordingPath);
    EXPECT_TRUE(fi.exists());
    EXPECT_GT(fi.size(), 0);
}

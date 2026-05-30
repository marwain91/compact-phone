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

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <thread>
#include <vector>

using namespace std::chrono_literals;

namespace {
std::string sipServer()
{
    if (const char *env = std::getenv("COMPACTPHONE_SIP_SERVER")) return env;
    return "asterisk:5060";
}

void putLE16(std::vector<char> &b, uint16_t v)
{
    b.push_back(static_cast<char>(v & 0xff));
    b.push_back(static_cast<char>((v >> 8) & 0xff));
}
void putLE32(std::vector<char> &b, uint32_t v)
{
    b.push_back(static_cast<char>(v & 0xff));
    b.push_back(static_cast<char>((v >> 8) & 0xff));
    b.push_back(static_cast<char>((v >> 16) & 0xff));
    b.push_back(static_cast<char>((v >> 24) & 0xff));
}
void putTag(std::vector<char> &b, const char *tag)
{
    for (int i = 0; i < 4; ++i) b.push_back(tag[i]);
}

// Write a minimal valid 8 kHz mono 16-bit PCM WAV (matching the ulaw/alaw call
// clock) so PJSIP's AudioMediaPlayer can open it. 0.5 s of silence is enough —
// we only assert the player attaches, not what is heard.
bool writeSilenceWav(const QString &path)
{
    const uint32_t sampleRate = 8000;
    const uint16_t channels = 1;
    const uint16_t bits = 16;
    const uint32_t samples = sampleRate / 2; // 0.5 s
    const uint32_t dataBytes = samples * channels * (bits / 8);

    std::vector<char> buf;
    putTag(buf, "RIFF");
    putLE32(buf, 36 + dataBytes);
    putTag(buf, "WAVE");
    putTag(buf, "fmt ");
    putLE32(buf, 16);                  // PCM fmt chunk size
    putLE16(buf, 1);                   // audio format = PCM
    putLE16(buf, channels);
    putLE32(buf, sampleRate);
    putLE32(buf, sampleRate * channels * (bits / 8)); // byte rate
    putLE16(buf, channels * (bits / 8));              // block align
    putLE16(buf, bits);
    putTag(buf, "data");
    putLE32(buf, dataBytes);
    buf.insert(buf.end(), dataBytes, 0); // silence

    std::ofstream out(path.toStdString(), std::ios::binary | std::ios::trunc);
    if (!out) return false;
    out.write(buf.data(), static_cast<std::streamsize>(buf.size()));
    return out.good();
}
} // namespace

class AudioPlaybackTest : public ::testing::Test {
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

// Stream a WAV file into a live echo call (600), then stop it. Pins
// CallManager's playAudioFile / stopAudioFile / isPlayingAudioFile against real
// PJSIP media — these ship but had no integration coverage.
TEST_F(AudioPlaybackTest, PlaysAndStopsWavFileOnActiveCall)
{
    const QString wav = QDir::temp().filePath("compactphone_test_play.wav");
    QFile::remove(wav);
    ASSERT_TRUE(writeSilenceWav(wav));

    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::Account a;
    a.displayName = "Play";
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
    ASSERT_FALSE(cm.isPlayingAudioFile(callId));

    // Media may need a moment after CONFIRMED before getMedia() is ACTIVE;
    // playAudioFile returns false until then, so poll the success path.
    bool playing = false;
    for (int i = 0; i < 50 && !playing; ++i) {
        playing = cm.playAudioFile(callId, wav.toStdString(), /*loop=*/false);
        if (!playing) std::this_thread::sleep_for(100ms);
    }
    EXPECT_TRUE(playing);
    EXPECT_TRUE(cm.isPlayingAudioFile(callId));

    EXPECT_TRUE(cm.stopAudioFile(callId));
    EXPECT_FALSE(cm.isPlayingAudioFile(callId));

    cm.hangup(callId);
    {
        std::unique_lock l(mtx);
        ASSERT_TRUE(cv.wait_for(l, 5s, [&] {
            return observed == compactphone::sip::CallState::Disconnected;
        }));
    }
    QFile::remove(wav);
}

// playAudioFile on an unknown call id returns false (no such call), and there is
// no phantom player to stop.
TEST_F(AudioPlaybackTest, PlayRejectsUnknownCallAndStopWithoutPlayer)
{
    compactphone::sip::AccountsManager am(&engine, &db, &kc);
    compactphone::sip::CallManager cm(&am);
    EXPECT_FALSE(cm.playAudioFile(9999, "/nonexistent.wav", false));
    EXPECT_FALSE(cm.isPlayingAudioFile(9999));
    EXPECT_FALSE(cm.stopAudioFile(9999)); // nothing to stop
}

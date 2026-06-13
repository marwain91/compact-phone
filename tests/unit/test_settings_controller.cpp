#include <gtest/gtest.h>

#include "core/SettingsController.h"
#include "core/SettingsManager.h"
#include "core/platform/Autostart_memory.h"
#include "core/sipbackend/fake/FakeSipBackend.h"
#include "persistence/Database.h"

#include <QTemporaryDir>

#include <memory>

class SettingsControllerTest : public ::testing::Test {
protected:
    compactphone::persistence::Database db;

    void SetUp() override { ASSERT_TRUE(db.openInMemory()); }
};

TEST_F(SettingsControllerTest, LoadsDefaultsAndPersistsSettingChanges)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    EXPECT_EQ(controller.logLevel(), QStringLiteral("info"));
    EXPECT_TRUE(controller.ringtoneEnabled());
    EXPECT_EQ(controller.themeId(), QStringLiteral("light"));
    EXPECT_EQ(controller.defaultRingtonePath(),
              tmp.path() + QStringLiteral("/ringtone.wav"));

    controller.setLogLevel(QStringLiteral("debug"));
    controller.setRingtoneEnabled(false);
    controller.setThemeId(QStringLiteral("dark"));

    EXPECT_EQ(settings.getOr("log_level", ""), "debug");
    EXPECT_EQ(settings.getOr("ringtone_enabled", ""), "0");
    EXPECT_EQ(settings.getOr("theme_id", ""), "dark");
}

TEST_F(SettingsControllerTest, IgnoresDuplicateValuesAndEmitsOnRealChanges)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    int logSignals = 0;
    int ringtoneSignals = 0;
    int themeSignals = 0;
    QObject::connect(&controller, &compactphone::SettingsController::logLevelChanged,
                     [&] { ++logSignals; });
    QObject::connect(&controller, &compactphone::SettingsController::ringtoneEnabledChanged,
                     [&] { ++ringtoneSignals; });
    QObject::connect(&controller, &compactphone::SettingsController::themeIdChanged,
                     [&] { ++themeSignals; });

    controller.setLogLevel(QStringLiteral("info"));
    controller.setRingtoneEnabled(true);
    controller.setThemeId(QStringLiteral("light"));
    EXPECT_EQ(logSignals, 0);
    EXPECT_EQ(ringtoneSignals, 0);
    EXPECT_EQ(themeSignals, 0);

    controller.setLogLevel(QStringLiteral("warn"));
    controller.setRingtoneEnabled(false);
    controller.setThemeId(QStringLiteral("dark"));
    EXPECT_EQ(logSignals, 1);
    EXPECT_EQ(ringtoneSignals, 1);
    EXPECT_EQ(themeSignals, 1);
}

TEST_F(SettingsControllerTest, MissingRingtonePathFallsBackToDefault)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    controller.setRingtonePath(tmp.path() + QStringLiteral("/missing.wav"));

    EXPECT_EQ(controller.ringtonePath(), controller.defaultRingtonePath());
    EXPECT_EQ(settings.getOr("ringtone_path", ""),
              controller.defaultRingtonePath().toStdString());
}

TEST_F(SettingsControllerTest, AudioDevicesRouteThroughBackend)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sipbackend::FakeSipBackend backend;
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(&backend, &settings, tmp.path());

    // The fake backend exposes one input (id 0) and one output (id 1) — the
    // first time the audio-device path is exercised in a unit test (it was
    // dead behind a null engine before the ISipBackend reroute).
    const auto inputs = controller.audioInputs();
    ASSERT_EQ(inputs.size(), 1);
    EXPECT_EQ(inputs[0].toMap().value("id").toInt(), 0);
    EXPECT_EQ(inputs[0].toMap().value("name").toString(),
              QStringLiteral("Fake Microphone"));
    const auto outputs = controller.audioOutputs();
    ASSERT_EQ(outputs.size(), 1);
    EXPECT_EQ(outputs[0].toMap().value("id").toInt(), 1);

    // A valid selection is applied through the backend and persisted.
    controller.setCaptureDeviceId(0);
    EXPECT_EQ(controller.captureDeviceId(), 0);
    EXPECT_EQ(settings.getOr("capture_device_id", ""), "0");
    controller.setPlaybackDeviceId(1);
    EXPECT_EQ(controller.playbackDeviceId(), 1);
    EXPECT_EQ(settings.getOr("playback_device_id", ""), "1");

    // An invalid selection (an output-only device offered as capture) is
    // rejected by the backend and leaves the current selection untouched.
    controller.setPlaybackDeviceId(0);
    EXPECT_EQ(controller.playbackDeviceId(), 1);
}

TEST_F(SettingsControllerTest, RingtonePlaysAndStopsThroughBackend)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sipbackend::FakeSipBackend backend;
    ASSERT_TRUE(backend.start(compactphone::sipbackend::EngineConfig{}));
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(&backend, &settings, tmp.path());

    // Ringtone enabled by default; not ringing yet -> backend not playing.
    EXPECT_TRUE(controller.ringtoneEnabled());
    EXPECT_TRUE(backend.ringtonePath().empty());

    // Ringing while enabled -> the backend plays the resolved ringtone path
    // (policy in the controller, playback in the backend).
    controller.setRinging(true);
    EXPECT_FALSE(backend.ringtonePath().empty());
    EXPECT_EQ(backend.ringtonePath(), controller.ringtonePath().toStdString());

    // Disabling the ringtone while ringing stops playback through the backend.
    controller.setRingtoneEnabled(false);
    EXPECT_TRUE(backend.ringtonePath().empty());
}

TEST_F(SettingsControllerTest, PersistsCallPolicySettingsAndClampsTimeouts)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    controller.setDndEnabled(true);
    controller.setAutoAnswerEnabled(true);
    controller.setAutoAnswerDelayMs(500);
    EXPECT_EQ(controller.autoAnswerDelayMs(), 500);
    EXPECT_EQ(settings.getOr("auto_answer_delay_ms", ""), "500");

    controller.setAutoAnswerDelayMs(-50);
    EXPECT_EQ(controller.autoAnswerDelayMs(), 0);
    EXPECT_EQ(settings.getOr("auto_answer_delay_ms", ""), "0");

    controller.setAutoAnswerDelayMs(90000);
    EXPECT_EQ(controller.autoAnswerDelayMs(), 60000);
    EXPECT_EQ(settings.getOr("auto_answer_delay_ms", ""), "60000");

    controller.setCfwdAlwaysEnabled(true);
    controller.setCfwdAlwaysTarget(QStringLiteral("sip:always@example.com"));
    controller.setCfwdBusyEnabled(true);
    controller.setCfwdBusyTarget(QStringLiteral("sip:busy@example.com"));
    controller.setCfwdNoAnswerEnabled(true);
    controller.setCfwdNoAnswerTarget(QStringLiteral("sip:later@example.com"));

    controller.setCfwdNoAnswerTimeoutMs(50);
    EXPECT_EQ(controller.cfwdNoAnswerTimeoutMs(), 1000);
    EXPECT_EQ(settings.getOr("cfwd_noanswer_timeout_ms", ""), "1000");

    controller.setCfwdNoAnswerTimeoutMs(500000);
    EXPECT_EQ(controller.cfwdNoAnswerTimeoutMs(), 120000);
    EXPECT_EQ(settings.getOr("cfwd_noanswer_timeout_ms", ""), "120000");

    EXPECT_TRUE(controller.dndEnabled());
    EXPECT_TRUE(controller.autoAnswerEnabled());
    EXPECT_TRUE(controller.cfwdAlwaysEnabled());
    EXPECT_EQ(controller.cfwdAlwaysTarget(), QStringLiteral("sip:always@example.com"));
    EXPECT_TRUE(controller.cfwdBusyEnabled());
    EXPECT_EQ(controller.cfwdBusyTarget(), QStringLiteral("sip:busy@example.com"));
    EXPECT_TRUE(controller.cfwdNoAnswerEnabled());
    EXPECT_EQ(controller.cfwdNoAnswerTarget(), QStringLiteral("sip:later@example.com"));

    EXPECT_EQ(settings.getOr("dnd_enabled", ""), "1");
    EXPECT_EQ(settings.getOr("auto_answer_enabled", ""), "1");
    EXPECT_EQ(settings.getOr("cfwd_always_enabled", ""), "1");
    EXPECT_EQ(settings.getOr("cfwd_always_target", ""), "sip:always@example.com");
    EXPECT_EQ(settings.getOr("cfwd_busy_enabled", ""), "1");
    EXPECT_EQ(settings.getOr("cfwd_busy_target", ""), "sip:busy@example.com");
    EXPECT_EQ(settings.getOr("cfwd_noanswer_enabled", ""), "1");
    EXPECT_EQ(settings.getOr("cfwd_noanswer_target", ""), "sip:later@example.com");
}

TEST_F(SettingsControllerTest, RecordingsPathDefaultsAndPersistsOverride)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    EXPECT_EQ(controller.recordingsPath(),
              tmp.path() + QStringLiteral("/recordings"));

    const auto customPath = tmp.path() + QStringLiteral("/custom-recordings");
    controller.setRecordingsPath(customPath);
    controller.setAutoRecordEnabled(true);

    EXPECT_EQ(controller.recordingsPath(), customPath);
    EXPECT_TRUE(controller.autoRecordEnabled());
    EXPECT_EQ(settings.getOr("recordings_path", ""), customPath.toStdString());
    EXPECT_EQ(settings.getOr("auto_record_enabled", ""), "1");
}

TEST_F(SettingsControllerTest, NewTogglesDefaultOff)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    EXPECT_FALSE(controller.enterpriseFeaturesEnabled());
    EXPECT_FALSE(controller.crashReportingEnabled());
}

TEST_F(SettingsControllerTest, CrashReportingTogglePersistsAndEmits)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    int emitCount = 0;
    QObject::connect(&controller,
                     &compactphone::SettingsController::crashReportingEnabledChanged,
                     [&] { ++emitCount; });

    controller.setCrashReportingEnabled(true);
    EXPECT_TRUE(controller.crashReportingEnabled());
    EXPECT_EQ(settings.getOr("crash_reporting_enabled", ""), "1");
    EXPECT_EQ(emitCount, 1);

    // Idempotent: re-setting to true is a no-op for signals.
    controller.setCrashReportingEnabled(true);
    EXPECT_EQ(emitCount, 1);

    controller.setCrashReportingEnabled(false);
    EXPECT_FALSE(controller.crashReportingEnabled());
    EXPECT_EQ(settings.getOr("crash_reporting_enabled", ""), "0");
    EXPECT_EQ(emitCount, 2);
}

TEST_F(SettingsControllerTest, SettingsSurviveControllerRestart)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());

    {
        compactphone::sip::SettingsManager settings(&db);
        compactphone::SettingsController controller(nullptr, &settings, tmp.path());
        controller.setThemeId(QStringLiteral("midnight"));
        controller.setLogLevel(QStringLiteral("warn"));
        controller.setCrashReportingEnabled(true);
        controller.setEnterpriseFeaturesEnabled(true);
    }

    // Fresh controller against the same DB should pick up the persisted values.
    compactphone::sip::SettingsManager settings2(&db);
    compactphone::SettingsController controller2(nullptr, &settings2, tmp.path());
    EXPECT_EQ(controller2.themeId(), "midnight");
    EXPECT_EQ(controller2.logLevel(), "warn");
    EXPECT_TRUE(controller2.crashReportingEnabled());
    EXPECT_TRUE(controller2.enterpriseFeaturesEnabled());
}

TEST_F(SettingsControllerTest, LaunchOnStartupReflectsBackendAndPersistsThroughIt)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    auto fake = std::make_unique<compactphone::platform::MemoryAutostart>();
    auto *fakePtr = fake.get();
    compactphone::SettingsController controller(
        nullptr, &settings, tmp.path(), std::move(fake));

    EXPECT_FALSE(controller.launchOnStartup());
    EXPECT_TRUE(controller.autostartSupported());

    int changes = 0;
    QObject::connect(&controller,
                     &compactphone::SettingsController::launchOnStartupChanged,
                     [&] { ++changes; });

    controller.setLaunchOnStartup(true);
    EXPECT_TRUE(controller.launchOnStartup());
    EXPECT_TRUE(fakePtr->isEnabled());      // went through the backend
    EXPECT_EQ(changes, 1);
}

TEST_F(SettingsControllerTest, LaunchOnStartupRevertsWhenBackendFails)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    auto fake = std::make_unique<compactphone::platform::MemoryAutostart>();
    auto *fakePtr = fake.get();
    fakePtr->failNextSetEnabled();
    compactphone::SettingsController controller(
        nullptr, &settings, tmp.path(), std::move(fake));

    int failures = 0;
    QObject::connect(&controller,
                     &compactphone::SettingsController::launchOnStartupFailed,
                     [&] { ++failures; });

    controller.setLaunchOnStartup(true);
    EXPECT_FALSE(controller.launchOnStartup());   // reverted
    EXPECT_FALSE(fakePtr->isEnabled());
    EXPECT_EQ(failures, 1);
}

TEST_F(SettingsControllerTest, AutostartSupportedFollowsBackend)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    auto fake = std::make_unique<compactphone::platform::MemoryAutostart>();
    fake->setSupported(false);
    compactphone::SettingsController controller(
        nullptr, &settings, tmp.path(), std::move(fake));
    EXPECT_FALSE(controller.autostartSupported());
}

TEST_F(SettingsControllerTest, StartMinimizedToTrayPersistsAndDefaultsOff)
{
    QTemporaryDir tmp;
    ASSERT_TRUE(tmp.isValid());
    compactphone::sip::SettingsManager settings(&db);
    compactphone::SettingsController controller(nullptr, &settings, tmp.path());

    EXPECT_FALSE(controller.startMinimizedToTray());

    int changes = 0;
    QObject::connect(&controller,
                     &compactphone::SettingsController::startMinimizedToTrayChanged,
                     [&] { ++changes; });

    controller.setStartMinimizedToTray(true);
    EXPECT_TRUE(controller.startMinimizedToTray());
    EXPECT_EQ(settings.getOr("start_minimized_to_tray", ""), "1");
    EXPECT_EQ(changes, 1);

    controller.setStartMinimizedToTray(true);   // no-op
    EXPECT_EQ(changes, 1);
}

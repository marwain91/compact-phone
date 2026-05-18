#include <gtest/gtest.h>

#include "core/SettingsController.h"
#include "core/SettingsManager.h"
#include "persistence/Database.h"

#include <QTemporaryDir>

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

#include "SettingsController.h"

#include "RingtonePlayer.h"
#include "SettingsManager.h"
#include "SipEngine.h"

#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

#include <string>
#include <utility>

namespace compactphone {

namespace {

void applyLogLevel(const QString &lvl)
{
    if (lvl == QStringLiteral("trace")) spdlog::set_level(spdlog::level::trace);
    else if (lvl == QStringLiteral("debug")) spdlog::set_level(spdlog::level::debug);
    else if (lvl == QStringLiteral("warn")) spdlog::set_level(spdlog::level::warn);
    else if (lvl == QStringLiteral("error")) spdlog::set_level(spdlog::level::err);
    else spdlog::set_level(spdlog::level::info);
}

} // namespace

SettingsController::SettingsController(sip::SipEngine *engine,
                                       sip::SettingsManager *settings,
                                       QString appDataPath,
                                       QObject *parent)
    : QObject(parent),
      m_engine(engine),
      m_settings(settings),
      m_appDataPath(std::move(appDataPath))
{
    if (m_settings) {
        m_logLevel = QString::fromStdString(m_settings->getOr("log_level", "info"));
        m_ringtoneEnabled = m_settings->getOr("ringtone_enabled", "1") != "0";
        m_themeId = QString::fromStdString(m_settings->getOr("theme_id", "light"));
        m_dndEnabled = m_settings->getOr("dnd_enabled", "0") == "1";
        m_autoAnswerEnabled = m_settings->getOr("auto_answer_enabled", "0") == "1";
        try {
            m_autoAnswerDelayMs = std::stoi(m_settings->getOr("auto_answer_delay_ms", "0"));
        } catch (...) { m_autoAnswerDelayMs = 0; }
        m_cfwdAlwaysEnabled = m_settings->getOr("cfwd_always_enabled", "0") == "1";
        m_cfwdAlwaysTarget = QString::fromStdString(m_settings->getOr("cfwd_always_target", ""));
        m_cfwdBusyEnabled = m_settings->getOr("cfwd_busy_enabled", "0") == "1";
        m_cfwdBusyTarget = QString::fromStdString(m_settings->getOr("cfwd_busy_target", ""));
        m_cfwdNoAnswerEnabled = m_settings->getOr("cfwd_noanswer_enabled", "0") == "1";
        m_cfwdNoAnswerTarget = QString::fromStdString(m_settings->getOr("cfwd_noanswer_target", ""));
        try {
            m_cfwdNoAnswerTimeoutMs = std::stoi(m_settings->getOr("cfwd_noanswer_timeout_ms", "20000"));
        } catch (...) { m_cfwdNoAnswerTimeoutMs = 20000; }
        m_autoRecordEnabled = m_settings->getOr("auto_record_enabled", "0") == "1";
        m_enterpriseFeaturesEnabled = m_settings->getOr("enterprise_features_enabled", "0") == "1";
        m_crashReportingEnabled = m_settings->getOr("crash_reporting_enabled", "0") == "1";
        m_alwaysOnTop = m_settings->getOr("always_on_top", "0") == "1";
        m_recordingsPath = QString::fromStdString(
            m_settings->getOr("recordings_path", ""));
    }
    applyLogLevel(m_logLevel);

    const auto defaultRing = defaultRingtonePath().toStdString();
    auto storedRing = m_settings
        ? m_settings->getOr("ringtone_path", defaultRing)
        : defaultRing;
    if (storedRing.empty() || !QFile::exists(QString::fromStdString(storedRing))) {
        storedRing = defaultRing;
    }
    m_ringtone = std::make_unique<sip::RingtonePlayer>(storedRing);

    try {
        if (!m_engine || !m_engine->endpoint() || !m_settings) return;
        auto &mgr = m_engine->endpoint()->audDevManager();
        const auto capStr = m_settings->getOr("capture_device_id", "");
        if (!capStr.empty()) mgr.setCaptureDev(std::stoi(capStr));
        const auto pbStr = m_settings->getOr("playback_device_id", "");
        if (!pbStr.empty()) mgr.setPlaybackDev(std::stoi(pbStr));
    } catch (...) {}
}

SettingsController::~SettingsController() = default;

void SettingsController::setLogLevel(const QString &lvl)
{
    if (m_logLevel == lvl) return;
    m_logLevel = lvl;
    if (m_settings) m_settings->set("log_level", lvl.toStdString());
    applyLogLevel(lvl);
    emit logLevelChanged();
}

void SettingsController::setRingtoneEnabled(bool enabled)
{
    if (m_ringtoneEnabled == enabled) return;
    m_ringtoneEnabled = enabled;
    if (m_settings) m_settings->set("ringtone_enabled", enabled ? "1" : "0");
    applyRingtoneState();
    emit ringtoneEnabledChanged();
}

void SettingsController::setThemeId(const QString &id)
{
    if (m_themeId == id) return;
    m_themeId = id;
    if (m_settings) m_settings->set("theme_id", id.toStdString());
    emit themeIdChanged();
}

void SettingsController::setDndEnabled(bool enabled)
{
    if (m_dndEnabled == enabled) return;
    m_dndEnabled = enabled;
    if (m_settings) m_settings->set("dnd_enabled", enabled ? "1" : "0");
    emit dndEnabledChanged();
}

void SettingsController::setAutoAnswerEnabled(bool enabled)
{
    if (m_autoAnswerEnabled == enabled) return;
    m_autoAnswerEnabled = enabled;
    if (m_settings) m_settings->set("auto_answer_enabled", enabled ? "1" : "0");
    emit autoAnswerEnabledChanged();
}

void SettingsController::setAutoAnswerDelayMs(int ms)
{
    const int clamped = ms < 0 ? 0 : (ms > 60000 ? 60000 : ms);
    if (m_autoAnswerDelayMs == clamped) return;
    m_autoAnswerDelayMs = clamped;
    if (m_settings) m_settings->set("auto_answer_delay_ms",
                                    std::to_string(clamped));
    emit autoAnswerDelayMsChanged();
}

void SettingsController::setCfwdAlwaysEnabled(bool enabled)
{
    if (m_cfwdAlwaysEnabled == enabled) return;
    m_cfwdAlwaysEnabled = enabled;
    if (m_settings) m_settings->set("cfwd_always_enabled", enabled ? "1" : "0");
    emit cfwdAlwaysEnabledChanged();
}

void SettingsController::setCfwdAlwaysTarget(const QString &uri)
{
    if (m_cfwdAlwaysTarget == uri) return;
    m_cfwdAlwaysTarget = uri;
    if (m_settings) m_settings->set("cfwd_always_target", uri.toStdString());
    emit cfwdAlwaysTargetChanged();
}

void SettingsController::setCfwdBusyEnabled(bool enabled)
{
    if (m_cfwdBusyEnabled == enabled) return;
    m_cfwdBusyEnabled = enabled;
    if (m_settings) m_settings->set("cfwd_busy_enabled", enabled ? "1" : "0");
    emit cfwdBusyEnabledChanged();
}

void SettingsController::setCfwdBusyTarget(const QString &uri)
{
    if (m_cfwdBusyTarget == uri) return;
    m_cfwdBusyTarget = uri;
    if (m_settings) m_settings->set("cfwd_busy_target", uri.toStdString());
    emit cfwdBusyTargetChanged();
}

void SettingsController::setCfwdNoAnswerEnabled(bool enabled)
{
    if (m_cfwdNoAnswerEnabled == enabled) return;
    m_cfwdNoAnswerEnabled = enabled;
    if (m_settings) m_settings->set("cfwd_noanswer_enabled", enabled ? "1" : "0");
    emit cfwdNoAnswerEnabledChanged();
}

void SettingsController::setCfwdNoAnswerTarget(const QString &uri)
{
    if (m_cfwdNoAnswerTarget == uri) return;
    m_cfwdNoAnswerTarget = uri;
    if (m_settings) m_settings->set("cfwd_noanswer_target", uri.toStdString());
    emit cfwdNoAnswerTargetChanged();
}

void SettingsController::setCfwdNoAnswerTimeoutMs(int ms)
{
    const int clamped = ms < 1000 ? 1000 : (ms > 120000 ? 120000 : ms);
    if (m_cfwdNoAnswerTimeoutMs == clamped) return;
    m_cfwdNoAnswerTimeoutMs = clamped;
    if (m_settings) m_settings->set("cfwd_noanswer_timeout_ms",
                                    std::to_string(clamped));
    emit cfwdNoAnswerTimeoutMsChanged();
}

void SettingsController::setAutoRecordEnabled(bool enabled)
{
    if (m_autoRecordEnabled == enabled) return;
    m_autoRecordEnabled = enabled;
    if (m_settings) m_settings->set("auto_record_enabled", enabled ? "1" : "0");
    emit autoRecordEnabledChanged();
}

QString SettingsController::recordingsPath() const
{
    if (!m_recordingsPath.isEmpty()) return m_recordingsPath;
    return m_appDataPath + QStringLiteral("/recordings");
}

void SettingsController::setRecordingsPath(const QString &p)
{
    if (m_recordingsPath == p) return;
    m_recordingsPath = p;
    if (m_settings) m_settings->set("recordings_path", p.toStdString());
    emit recordingsPathChanged();
}

void SettingsController::setEnterpriseFeaturesEnabled(bool enabled)
{
    if (m_enterpriseFeaturesEnabled == enabled) return;
    m_enterpriseFeaturesEnabled = enabled;
    if (m_settings) m_settings->set("enterprise_features_enabled",
                                    enabled ? "1" : "0");
    emit enterpriseFeaturesEnabledChanged();
}

void SettingsController::setCrashReportingEnabled(bool enabled)
{
    if (m_crashReportingEnabled == enabled) return;
    m_crashReportingEnabled = enabled;
    if (m_settings) m_settings->set("crash_reporting_enabled",
                                    enabled ? "1" : "0");
    emit crashReportingEnabledChanged();
}

void SettingsController::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled) return;
    m_alwaysOnTop = enabled;
    if (m_settings) m_settings->set("always_on_top", enabled ? "1" : "0");
    emit alwaysOnTopChanged();
}

QVariantList SettingsController::audioInputs() const
{
    QVariantList out;
    if (!m_engine || !m_engine->endpoint()) return out;
    try {
        auto &mgr = m_engine->endpoint()->audDevManager();
        const auto devs = mgr.enumDev2();
        for (size_t i = 0; i < devs.size(); ++i) {
            if (devs[i].inputCount <= 0) continue;
            QVariantMap m;
            m["id"] = static_cast<int>(i);
            m["name"] = QString::fromStdString(devs[i].name);
            out.append(m);
        }
    } catch (...) {}
    return out;
}

QVariantList SettingsController::audioOutputs() const
{
    QVariantList out;
    if (!m_engine || !m_engine->endpoint()) return out;
    try {
        auto &mgr = m_engine->endpoint()->audDevManager();
        const auto devs = mgr.enumDev2();
        for (size_t i = 0; i < devs.size(); ++i) {
            if (devs[i].outputCount <= 0) continue;
            QVariantMap m;
            m["id"] = static_cast<int>(i);
            m["name"] = QString::fromStdString(devs[i].name);
            out.append(m);
        }
    } catch (...) {}
    return out;
}

int SettingsController::captureDeviceId() const
{
    if (!m_engine || !m_engine->endpoint()) return -1;
    try { return m_engine->endpoint()->audDevManager().getCaptureDev(); }
    catch (...) { return -1; }
}

int SettingsController::playbackDeviceId() const
{
    if (!m_engine || !m_engine->endpoint()) return -1;
    try { return m_engine->endpoint()->audDevManager().getPlaybackDev(); }
    catch (...) { return -1; }
}

void SettingsController::setCaptureDeviceId(int id)
{
    if (!m_engine || !m_engine->endpoint()) return;
    try {
        m_engine->endpoint()->audDevManager().setCaptureDev(id);
        if (m_settings) m_settings->set("capture_device_id", std::to_string(id));
        emit captureDeviceIdChanged();
    } catch (const pj::Error &e) {
        spdlog::error("setCaptureDeviceId: {}", e.info());
    }
}

void SettingsController::setPlaybackDeviceId(int id)
{
    if (!m_engine || !m_engine->endpoint()) return;
    try {
        m_engine->endpoint()->audDevManager().setPlaybackDev(id);
        if (m_settings) m_settings->set("playback_device_id", std::to_string(id));
        emit playbackDeviceIdChanged();
    } catch (const pj::Error &e) {
        spdlog::error("setPlaybackDeviceId: {}", e.info());
    }
}

void SettingsController::refreshAudioDevices()
{
    if (!m_engine || !m_engine->endpoint()) return;
    try { m_engine->endpoint()->audDevManager().refreshDevs(); }
    catch (...) {}
    emit audioDevicesChanged();
    emit captureDeviceIdChanged();
    emit playbackDeviceIdChanged();
}

void SettingsController::testRingtone(int durationMs)
{
    if (!m_ringtone) return;
    m_ringtone->start();
    QTimer::singleShot(durationMs, this, [this] {
        if (m_ringtone) applyRingtoneState();
    });
}

QString SettingsController::ringtonePath() const
{
    return m_ringtone ? QString::fromStdString(m_ringtone->path()) : QString{};
}

void SettingsController::setRingtonePath(const QString &p)
{
    if (!m_ringtone) return;
    QString path = p;
    if (path.startsWith("file://")) path = QUrl(path).toLocalFile();
    if (path.isEmpty() || !QFile::exists(path)) {
        path = defaultRingtonePath();
    }
    if (QString::fromStdString(m_ringtone->path()) == path) {
        if (m_settings) m_settings->set("ringtone_path", path.toStdString());
        return;
    }
    m_ringtone->setPath(path.toStdString());
    if (m_settings) m_settings->set("ringtone_path", path.toStdString());
    applyRingtoneState();
    emit ringtonePathChanged();
}

QString SettingsController::defaultRingtonePath() const
{
    return m_appDataPath + QStringLiteral("/ringtone.wav");
}

void SettingsController::setRinging(bool ringing)
{
    if (m_ringing == ringing) return;
    m_ringing = ringing;
    applyRingtoneState();
}

void SettingsController::applyRingtoneState()
{
    if (!m_ringtone) return;
    if (m_ringtoneEnabled && m_ringing) m_ringtone->start();
    else                               m_ringtone->stop();
}

} // namespace compactphone

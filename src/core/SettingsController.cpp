#include "SettingsController.h"

#include "CrashReporting.h"
#include "SettingsManager.h"
#include "sipbackend/ISipBackend.h"
#include "platform/Autostart.h"
#include "platform/Autostart_factory.h"

#include <QFile>
#include <QTimer>
#include <QUrl>
#include <QVariantMap>

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

SettingsController::SettingsController(sipbackend::ISipBackend *backend,
                                       sip::SettingsManager *settings,
                                       QString appDataPath,
                                       QObject *parent)
    : SettingsController(backend, settings, std::move(appDataPath),
                         platform::makeAutostart(), parent)
{
}

SettingsController::SettingsController(sipbackend::ISipBackend *backend,
                                       sip::SettingsManager *settings,
                                       QString appDataPath,
                                       std::unique_ptr<platform::IAutostart> autostart,
                                       QObject *parent)
    : QObject(parent),
      m_backend(backend),
      m_settings(settings),
      m_appDataPath(std::move(appDataPath)),
      m_autostart(std::move(autostart))
{
    // OS login item is the source of truth for the toggle's initial state.
    m_launchOnStartup = m_autostart && m_autostart->isEnabled();
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
        m_startMinimizedToTray =
            m_settings->getOr("start_minimized_to_tray", "0") == "1";
        m_autoUpdateCheckEnabled =
            m_settings->getOr("update_check_on_startup", "1") != "0";
        m_skippedUpdateVersion = QString::fromStdString(
            m_settings->getOr("skipped_update_version", ""));
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
    m_ringtonePath = QString::fromStdString(storedRing);

    if (m_backend && m_settings) {
        const auto capStr = m_settings->getOr("capture_device_id", "");
        if (!capStr.empty()) {
            try { m_backend->setCaptureDevice(std::stoi(capStr)); }
            catch (...) { spdlog::warn("SettingsController: bad stored capture_device_id"); }
        }
        const auto pbStr = m_settings->getOr("playback_device_id", "");
        if (!pbStr.empty()) {
            try { m_backend->setPlaybackDevice(std::stoi(pbStr)); }
            catch (...) { spdlog::warn("SettingsController: bad stored playback_device_id"); }
        }
    }
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
    // Bring Sentry up immediately when the user opts in at runtime (no-op if
    // the build wasn't configured with a DSN).
    if (enabled) crash::initConfiguredSentry(true);
    emit crashReportingEnabledChanged();
}

void SettingsController::setAlwaysOnTop(bool enabled)
{
    if (m_alwaysOnTop == enabled) return;
    m_alwaysOnTop = enabled;
    if (m_settings) m_settings->set("always_on_top", enabled ? "1" : "0");
    emit alwaysOnTopChanged();
}

bool SettingsController::autostartSupported() const
{
    return m_autostart && m_autostart->isSupported();
}

void SettingsController::setLaunchOnStartup(bool enabled)
{
    if (m_launchOnStartup == enabled) return;
    if (!m_autostart || !m_autostart->setEnabled(enabled)) {
        spdlog::warn("SettingsController: failed to {} launch-on-startup",
                     enabled ? "enable" : "disable");
        // Leave m_launchOnStartup unchanged; emit Changed so a bound QML
        // switch snaps back to the real state, and Failed for a notice.
        emit launchOnStartupFailed(
            enabled ? QStringLiteral("Couldn't enable launch on startup.")
                    : QStringLiteral("Couldn't disable launch on startup."));
        emit launchOnStartupChanged();
        return;
    }
    m_launchOnStartup = enabled;
    emit launchOnStartupChanged();
}

void SettingsController::setStartMinimizedToTray(bool enabled)
{
    if (m_startMinimizedToTray == enabled) return;
    m_startMinimizedToTray = enabled;
    if (m_settings) m_settings->set("start_minimized_to_tray",
                                    enabled ? "1" : "0");
    emit startMinimizedToTrayChanged();
}

void SettingsController::setAutoUpdateCheckEnabled(bool enabled)
{
    if (m_autoUpdateCheckEnabled == enabled) return;
    m_autoUpdateCheckEnabled = enabled;
    if (m_settings) m_settings->set("update_check_on_startup",
                                    enabled ? "1" : "0");
    emit autoUpdateCheckEnabledChanged();
}

void SettingsController::setSkippedUpdateVersion(const QString &version)
{
    if (m_skippedUpdateVersion == version) return;
    m_skippedUpdateVersion = version;
    if (m_settings) m_settings->set("skipped_update_version",
                                    version.toStdString());
    emit skippedUpdateVersionChanged();
}

qint64 SettingsController::lastUpdateCheckMs() const
{
    if (!m_settings) return 0;
    try {
        return std::stoll(m_settings->getOr("last_update_check_ms", "0"));
    } catch (...) {
        return 0;
    }
}

void SettingsController::setLastUpdateCheckMs(qint64 ms)
{
    if (m_settings) m_settings->set("last_update_check_ms",
                                    std::to_string(ms));
}

QVariantList SettingsController::audioInputs() const
{
    QVariantList out;
    if (!m_backend) return out;
    for (const auto &d : m_backend->audioDevices()) {
        if (d.inputCount <= 0) continue;
        QVariantMap m;
        m["id"] = d.id;
        m["name"] = QString::fromStdString(d.name);
        out.append(m);
    }
    return out;
}

QVariantList SettingsController::audioOutputs() const
{
    QVariantList out;
    if (!m_backend) return out;
    for (const auto &d : m_backend->audioDevices()) {
        if (d.outputCount <= 0) continue;
        QVariantMap m;
        m["id"] = d.id;
        m["name"] = QString::fromStdString(d.name);
        out.append(m);
    }
    return out;
}

int SettingsController::captureDeviceId() const
{
    return m_backend ? m_backend->captureDevice() : -1;
}

int SettingsController::playbackDeviceId() const
{
    return m_backend ? m_backend->playbackDevice() : -1;
}

void SettingsController::setCaptureDeviceId(int id)
{
    if (!m_backend || !m_backend->setCaptureDevice(id)) return;
    if (m_settings) m_settings->set("capture_device_id", std::to_string(id));
    emit captureDeviceIdChanged();
}

void SettingsController::setPlaybackDeviceId(int id)
{
    if (!m_backend || !m_backend->setPlaybackDevice(id)) return;
    if (m_settings) m_settings->set("playback_device_id", std::to_string(id));
    emit playbackDeviceIdChanged();
}

void SettingsController::refreshAudioDevices()
{
    if (m_backend) m_backend->refreshAudioDevices();
    emit audioDevicesChanged();
    emit captureDeviceIdChanged();
    emit playbackDeviceIdChanged();
}

void SettingsController::testRingtone(int durationMs)
{
    if (m_backend) m_backend->playRingtone(m_ringtonePath.toStdString());
    QTimer::singleShot(durationMs, this, [this] { applyRingtoneState(); });
}

QString SettingsController::ringtonePath() const
{
    return m_ringtonePath;
}

void SettingsController::setRingtonePath(const QString &p)
{
    QString path = p;
    if (path.startsWith("file://")) path = QUrl(path).toLocalFile();
    if (path.isEmpty() || !QFile::exists(path)) {
        path = defaultRingtonePath();
    }
    if (m_ringtonePath == path) {
        if (m_settings) m_settings->set("ringtone_path", path.toStdString());
        return;
    }
    m_ringtonePath = path;
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
    if (!m_backend) return;
    if (m_ringtoneEnabled && m_ringing)
        m_backend->playRingtone(m_ringtonePath.toStdString());
    else
        m_backend->stopRingtone();
}

} // namespace compactphone

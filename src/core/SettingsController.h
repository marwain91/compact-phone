#pragma once

#include <QObject>
#include <QString>
#include <QVariantList>

#include <memory>

namespace compactphone::sip {
class RingtonePlayer;
class SettingsManager;
class SipEngine;
}

namespace compactphone {

class SettingsController : public QObject {
    Q_OBJECT
public:
    explicit SettingsController(sip::SipEngine *engine,
                                sip::SettingsManager *settings,
                                QString appDataPath,
                                QObject *parent = nullptr);
    ~SettingsController() override;

    QString logLevel() const { return m_logLevel; }
    void setLogLevel(const QString &lvl);
    bool ringtoneEnabled() const { return m_ringtoneEnabled; }
    void setRingtoneEnabled(bool enabled);
    QString themeId() const { return m_themeId; }
    void setThemeId(const QString &id);

    QVariantList audioInputs() const;
    QVariantList audioOutputs() const;
    int captureDeviceId() const;
    int playbackDeviceId() const;
    void setCaptureDeviceId(int id);
    void setPlaybackDeviceId(int id);
    void refreshAudioDevices();
    void testRingtone(int durationMs = 2000);

    QString ringtonePath() const;
    void setRingtonePath(const QString &p);
    QString defaultRingtonePath() const;

    bool dndEnabled() const { return m_dndEnabled; }
    void setDndEnabled(bool enabled);

    bool autoAnswerEnabled() const { return m_autoAnswerEnabled; }
    void setAutoAnswerEnabled(bool enabled);

    int autoAnswerDelayMs() const { return m_autoAnswerDelayMs; }
    void setAutoAnswerDelayMs(int ms);

    // Call forwarding. Three modes, each with its own target URI.
    bool cfwdAlwaysEnabled() const { return m_cfwdAlwaysEnabled; }
    void setCfwdAlwaysEnabled(bool enabled);
    QString cfwdAlwaysTarget() const { return m_cfwdAlwaysTarget; }
    void setCfwdAlwaysTarget(const QString &uri);

    bool cfwdBusyEnabled() const { return m_cfwdBusyEnabled; }
    void setCfwdBusyEnabled(bool enabled);
    QString cfwdBusyTarget() const { return m_cfwdBusyTarget; }
    void setCfwdBusyTarget(const QString &uri);

    bool cfwdNoAnswerEnabled() const { return m_cfwdNoAnswerEnabled; }
    void setCfwdNoAnswerEnabled(bool enabled);
    QString cfwdNoAnswerTarget() const { return m_cfwdNoAnswerTarget; }
    void setCfwdNoAnswerTarget(const QString &uri);
    int cfwdNoAnswerTimeoutMs() const { return m_cfwdNoAnswerTimeoutMs; }
    void setCfwdNoAnswerTimeoutMs(int ms);

    bool autoRecordEnabled() const { return m_autoRecordEnabled; }
    void setAutoRecordEnabled(bool enabled);
    // Folder where call recordings are written. Defaults to
    // <AppDataLocation>/recordings.
    QString recordingsPath() const;
    void setRecordingsPath(const QString &p);

    // Gates Messages (SIP MESSAGE / IM) and Lines (BLF / presence) in
    // the sidebar. Both are contact-center / PBX features most personal
    // softphone users never touch — off by default.
    bool enterpriseFeaturesEnabled() const { return m_enterpriseFeaturesEnabled; }
    void setEnterpriseFeaturesEnabled(bool enabled);

    // Pins the main window above other application windows. When off
    // (default), the window behaves like any other. The incoming-call
    // dialog briefly forces on-top regardless of this setting so the
    // user notices the call even over a fullscreen app.
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    void setAlwaysOnTop(bool enabled);

    // Sentry / crash-report opt-in. Off by default; only honored when the
    // build was configured with -DCOMPACTPHONE_ENABLE_SENTRY=ON.
    bool crashReportingEnabled() const { return m_crashReportingEnabled; }
    void setCrashReportingEnabled(bool enabled);

public slots:
    void setRinging(bool ringing);

signals:
    void logLevelChanged();
    void ringtoneEnabledChanged();
    void themeIdChanged();
    void audioDevicesChanged();
    void captureDeviceIdChanged();
    void playbackDeviceIdChanged();
    void ringtonePathChanged();
    void dndEnabledChanged();
    void autoAnswerEnabledChanged();
    void autoAnswerDelayMsChanged();
    void cfwdAlwaysEnabledChanged();
    void cfwdAlwaysTargetChanged();
    void cfwdBusyEnabledChanged();
    void cfwdBusyTargetChanged();
    void cfwdNoAnswerEnabledChanged();
    void cfwdNoAnswerTargetChanged();
    void cfwdNoAnswerTimeoutMsChanged();
    void autoRecordEnabledChanged();
    void recordingsPathChanged();
    void enterpriseFeaturesEnabledChanged();
    void crashReportingEnabledChanged();
    void alwaysOnTopChanged();

private:
    sip::SipEngine *m_engine = nullptr;
    sip::SettingsManager *m_settings = nullptr;
    QString m_appDataPath;
    QString m_logLevel = QStringLiteral("info");
    bool m_ringtoneEnabled = true;
    bool m_ringing = false;
    QString m_themeId = QStringLiteral("light");
    bool m_dndEnabled = false;
    bool m_autoAnswerEnabled = false;
    int m_autoAnswerDelayMs = 0;
    bool m_cfwdAlwaysEnabled = false;
    QString m_cfwdAlwaysTarget;
    bool m_cfwdBusyEnabled = false;
    QString m_cfwdBusyTarget;
    bool m_cfwdNoAnswerEnabled = false;
    QString m_cfwdNoAnswerTarget;
    int m_cfwdNoAnswerTimeoutMs = 20000;
    bool m_autoRecordEnabled = false;
    QString m_recordingsPath;
    bool m_enterpriseFeaturesEnabled = false;
    bool m_crashReportingEnabled = false;
    bool m_alwaysOnTop = false;
    std::unique_ptr<sip::RingtonePlayer> m_ringtone;

    void applyRingtoneState();
};

} // namespace compactphone

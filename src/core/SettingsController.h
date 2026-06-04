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

namespace compactphone::platform {
class IAutostart;
}

namespace compactphone {

class SettingsController : public QObject {
    Q_OBJECT
    // Exposed to QML as PhoneController.settings (see PhoneController). Not
    // creatable from QML — owned by PhoneController.
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(bool ringtoneEnabled READ ringtoneEnabled WRITE setRingtoneEnabled NOTIFY ringtoneEnabledChanged)
    Q_PROPERTY(QString themeId READ themeId WRITE setThemeId NOTIFY themeIdChanged)
    Q_PROPERTY(QVariantList audioInputs READ audioInputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(QVariantList audioOutputs READ audioOutputs NOTIFY audioDevicesChanged)
    Q_PROPERTY(int captureDeviceId READ captureDeviceId WRITE setCaptureDeviceId NOTIFY captureDeviceIdChanged)
    Q_PROPERTY(int playbackDeviceId READ playbackDeviceId WRITE setPlaybackDeviceId NOTIFY playbackDeviceIdChanged)
    Q_PROPERTY(QString ringtonePath READ ringtonePath WRITE setRingtonePath NOTIFY ringtonePathChanged)
    Q_PROPERTY(QString defaultRingtonePath READ defaultRingtonePath CONSTANT)
    Q_PROPERTY(bool dndEnabled READ dndEnabled WRITE setDndEnabled NOTIFY dndEnabledChanged)
    Q_PROPERTY(bool autoAnswerEnabled READ autoAnswerEnabled WRITE setAutoAnswerEnabled NOTIFY autoAnswerEnabledChanged)
    Q_PROPERTY(int autoAnswerDelayMs READ autoAnswerDelayMs WRITE setAutoAnswerDelayMs NOTIFY autoAnswerDelayMsChanged)
    Q_PROPERTY(bool cfwdAlwaysEnabled READ cfwdAlwaysEnabled WRITE setCfwdAlwaysEnabled NOTIFY cfwdAlwaysEnabledChanged)
    Q_PROPERTY(QString cfwdAlwaysTarget READ cfwdAlwaysTarget WRITE setCfwdAlwaysTarget NOTIFY cfwdAlwaysTargetChanged)
    Q_PROPERTY(bool cfwdBusyEnabled READ cfwdBusyEnabled WRITE setCfwdBusyEnabled NOTIFY cfwdBusyEnabledChanged)
    Q_PROPERTY(QString cfwdBusyTarget READ cfwdBusyTarget WRITE setCfwdBusyTarget NOTIFY cfwdBusyTargetChanged)
    Q_PROPERTY(bool cfwdNoAnswerEnabled READ cfwdNoAnswerEnabled WRITE setCfwdNoAnswerEnabled NOTIFY cfwdNoAnswerEnabledChanged)
    Q_PROPERTY(QString cfwdNoAnswerTarget READ cfwdNoAnswerTarget WRITE setCfwdNoAnswerTarget NOTIFY cfwdNoAnswerTargetChanged)
    Q_PROPERTY(int cfwdNoAnswerTimeoutMs READ cfwdNoAnswerTimeoutMs WRITE setCfwdNoAnswerTimeoutMs NOTIFY cfwdNoAnswerTimeoutMsChanged)
    Q_PROPERTY(bool autoRecordEnabled READ autoRecordEnabled WRITE setAutoRecordEnabled NOTIFY autoRecordEnabledChanged)
    Q_PROPERTY(bool enterpriseFeaturesEnabled READ enterpriseFeaturesEnabled WRITE setEnterpriseFeaturesEnabled NOTIFY enterpriseFeaturesEnabledChanged)
    Q_PROPERTY(bool crashReportingEnabled READ crashReportingEnabled WRITE setCrashReportingEnabled NOTIFY crashReportingEnabledChanged)
    Q_PROPERTY(bool alwaysOnTop READ alwaysOnTop WRITE setAlwaysOnTop NOTIFY alwaysOnTopChanged)
    Q_PROPERTY(bool launchOnStartup READ launchOnStartup WRITE setLaunchOnStartup NOTIFY launchOnStartupChanged)
    Q_PROPERTY(bool autostartSupported READ autostartSupported CONSTANT)
public:
    explicit SettingsController(sip::SipEngine *engine,
                                sip::SettingsManager *settings,
                                QString appDataPath,
                                QObject *parent = nullptr);
    // Test seam: inject a specific autostart backend. Production uses the
    // 4-arg overload above, which selects the platform backend itself — so
    // call sites never need IAutostart to be a complete type.
    SettingsController(sip::SipEngine *engine,
                       sip::SettingsManager *settings,
                       QString appDataPath,
                       std::unique_ptr<platform::IAutostart> autostart,
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
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void testRingtone(int durationMs = 2000);

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

    // Registers/removes an OS login item. The OS entry is the source of
    // truth — launchOnStartup() reflects the backend's isEnabled(). A
    // failed setEnabled() reverts the toggle and emits launchOnStartupFailed.
    bool launchOnStartup() const { return m_launchOnStartup; }
    void setLaunchOnStartup(bool enabled);
    bool autostartSupported() const;

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
    void launchOnStartupChanged();
    void launchOnStartupFailed(const QString &message);

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
    bool m_launchOnStartup = false;
    std::unique_ptr<platform::IAutostart> m_autostart;
    std::unique_ptr<sip::RingtonePlayer> m_ringtone;

    void applyRingtoneState();
};

} // namespace compactphone

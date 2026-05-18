#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>
#include <QtQmlIntegration>

#include <memory>

namespace compactphone::persistence { class Database; }
namespace compactphone::platform { class IKeychain; }
namespace compactphone::models {
class AccountsModel;
class CallsModel;
class ContactsModel;
class HistoryModel;
class LinesModel;
class MessagesModel;
class ConversationsModel;
}

namespace compactphone::sip {
class SipEngine;
class AccountsManager;
class CallManager;
class ContactsManager;
class HistoryManager;
class LinesManager;
class MessagesManager;
class SettingsManager;
}

namespace compactphone {

class AccountsController;
class CallsController;
class SettingsController;
class TrayController;
class NetworkMonitor;
class PowerMonitor;
class UpdateChecker;

} // namespace compactphone

namespace compactphone::provisioning {
class Registry;
class Provider;
}

namespace compactphone {

class PhoneController : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString callState READ callState NOTIFY callStateChanged)
    Q_PROPERTY(QAbstractListModel *accounts READ accountsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *calls READ callsModel CONSTANT)
    Q_PROPERTY(int incomingCallId READ incomingCallId NOTIFY incomingCallChanged)
    Q_PROPERTY(QString incomingCallFrom READ incomingCallFrom NOTIFY incomingCallChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QAbstractListModel *contacts READ contactsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *history READ historyModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *conversations READ conversationsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *messages READ messagesModel CONSTANT)
    Q_PROPERTY(int unreadMessageCount READ unreadMessageCount NOTIFY unreadMessageCountChanged)
    Q_PROPERTY(QAbstractListModel *lines READ linesModel CONSTANT)
    Q_PROPERTY(QString dialerUri READ dialerUri WRITE setDialerUri NOTIFY dialerUriChanged)
    Q_PROPERTY(QString logLevel READ logLevel WRITE setLogLevel NOTIFY logLevelChanged)
    Q_PROPERTY(bool ringtoneEnabled READ ringtoneEnabled WRITE setRingtoneEnabled NOTIFY ringtoneEnabledChanged)
    Q_PROPERTY(QString themeId READ themeId WRITE setThemeId NOTIFY themeIdChanged)
    Q_PROPERTY(int registeredAccountCount READ registeredAccountCount NOTIFY registeredAccountCountChanged)
    Q_PROPERTY(int activeAccountId READ activeAccountId WRITE setActiveAccountId NOTIFY activeAccountIdChanged)
    Q_PROPERTY(int newVoicemailCount READ newVoicemailCount NOTIFY voicemailStateChanged)
    Q_PROPERTY(QString activeVoicemailNumber READ activeVoicemailNumber NOTIFY voicemailStateChanged)
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
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit PhoneController(QObject *parent = nullptr);
    ~PhoneController() override;

    QString callState() const;
    QAbstractListModel *accountsModel() const;
    QAbstractListModel *callsModel() const;

    Q_INVOKABLE int addAccount(const QVariantMap &params);
    Q_INVOKABLE bool removeAccount(int accountId);
    Q_INVOKABLE bool updateAccount(int accountId, const QVariantMap &params);
    Q_INVOKABLE QVariantMap accountSnapshot(int accountId) const;
    Q_INVOKABLE bool setDefaultAccount(int accountId);
    Q_INVOKABLE bool setAccountEnabled(int accountId, bool enabled);

    Q_INVOKABLE void dial(const QString &uri);
    Q_INVOKABLE void hangup(int callId);

    Q_INVOKABLE bool hold(int callId);
    Q_INVOKABLE bool unhold(int callId);
    Q_INVOKABLE bool setMuted(int callId, bool muted);
    Q_INVOKABLE bool sendDtmf(int callId, const QString &digits);

    int incomingCallId() const;
    QString incomingCallFrom() const;
    Q_INVOKABLE bool acceptIncoming();
    Q_INVOKABLE bool declineIncoming();

    QString notice() const { return m_notice; }
    Q_INVOKABLE bool blindTransfer(int callId, const QString &targetUri);
    Q_INVOKABLE bool attendedTransfer(int activeCallId, int destCallId);
    Q_INVOKABLE bool mergeCalls(int activeCallId, int heldCallId);

    Q_INVOKABLE bool startRecording(int callId);
    Q_INVOKABLE bool stopRecording(int callId);
    Q_INVOKABLE bool isRecording(int callId) const;

    bool autoRecordEnabled() const;
    void setAutoRecordEnabled(bool enabled);
    bool enterpriseFeaturesEnabled() const;
    void setEnterpriseFeaturesEnabled(bool enabled);
    bool crashReportingEnabled() const;
    void setCrashReportingEnabled(bool enabled);

    // Returns the first held confirmed call other than excludeCallId, or
    // -1 if none — used by the UI to enable the Merge button.
    Q_INVOKABLE int firstHeldCallId(int excludeCallId) const;
    Q_INVOKABLE void dismissNotice();

    QAbstractListModel *contactsModel() const;
    QAbstractListModel *historyModel() const;
    QAbstractListModel *conversationsModel() const;
    QAbstractListModel *messagesModel() const;
    QAbstractListModel *linesModel() const;
    int unreadMessageCount() const;

    Q_INVOKABLE int addWatchedLine(const QString &uri, const QString &label);
    Q_INVOKABLE bool removeWatchedLine(int lineId);
    Q_INVOKABLE void dialLine(int lineId);

    Q_INVOKABLE bool sendMessage(const QString &peerUri,
                                 const QString &body);
    Q_INVOKABLE void selectConversation(const QString &peerUri);
    Q_INVOKABLE void markConversationRead(const QString &peerUri);
    QString dialerUri() const { return m_dialerUri; }
    void setDialerUri(const QString &u);

    Q_INVOKABLE int addContact(const QString &displayName,
                               const QString &sipUri,
                               const QString &phone);
    Q_INVOKABLE int importContactsFromFile(const QString &path);

    Q_INVOKABLE QStringList recentLogLines() const;
    Q_INVOKABLE bool exportDiagnostics(const QString &path) const;
    Q_INVOKABLE void checkForUpdates();

    // Generic auto-provisioning. providerId selects a backend (e.g. "daktela").
    // On success a new SIP account is added via addAccount() and its id is
    // delivered via accountProvisioned.
    Q_INVOKABLE void provisionWithProvider(const QString &providerId,
                                           const QString &host,
                                           const QString &username,
                                           const QString &password);

    // Skip the password step and provision directly from an SSO access token.
    Q_INVOKABLE void provisionWithProviderToken(const QString &providerId,
                                                const QString &host,
                                                const QString &accessToken);

    // Probe the host to learn which auth methods are enabled. Emits
    // authMethodsDiscovered or authMethodsFailed.
    Q_INVOKABLE void discoverAuthMethods(const QString &providerId,
                                         const QString &host);

    // Descriptors for every built-in provider; each entry is
    // {"id", "displayName", "hostPlaceholder"}.
    Q_INVOKABLE QVariantList provisioningProviders() const;

    // Live RTCP-derived media stats for the in-call quality indicator.
    // Returns a QVariantMap with keys: mos (double), lossPct (double),
    // rttMs (int), jitterMs (int). Missing/unavailable fields are -1.
    Q_INVOKABLE QVariantMap streamStats(int callId) const;
    Q_INVOKABLE bool updateContact(int contactId,
                                   const QString &displayName,
                                   const QString &sipUri,
                                   const QString &phone);
    Q_INVOKABLE bool removeContact(int contactId);
    Q_INVOKABLE bool setContactFavorite(int contactId, bool favorite);
    Q_INVOKABLE void dialContact(int contactId);
    Q_INVOKABLE void redialFromHistory(int historyId);

    QString logLevel() const;
    void setLogLevel(const QString &lvl);
    bool ringtoneEnabled() const;
    void setRingtoneEnabled(bool enabled);
    QString themeId() const;
    void setThemeId(const QString &id);
    int registeredAccountCount() const;
    int activeAccountId() const;
    void setActiveAccountId(int id);

    int newVoicemailCount() const;
    QString activeVoicemailNumber() const;
    Q_INVOKABLE void dialVoicemail();

    QVariantList audioInputs() const;
    QVariantList audioOutputs() const;
    int captureDeviceId() const;
    int playbackDeviceId() const;
    void setCaptureDeviceId(int id);
    void setPlaybackDeviceId(int id);
    Q_INVOKABLE void refreshAudioDevices();
    Q_INVOKABLE void testRingtone(int durationMs = 2000);
    Q_INVOKABLE void requestQuit();

    QString ringtonePath() const;
    void setRingtonePath(const QString &p);
    QString defaultRingtonePath() const;

    bool dndEnabled() const;
    void setDndEnabled(bool enabled);
    bool autoAnswerEnabled() const;
    void setAutoAnswerEnabled(bool enabled);
    int autoAnswerDelayMs() const;
    void setAutoAnswerDelayMs(int ms);

    bool cfwdAlwaysEnabled() const;
    void setCfwdAlwaysEnabled(bool enabled);
    QString cfwdAlwaysTarget() const;
    void setCfwdAlwaysTarget(const QString &uri);
    bool cfwdBusyEnabled() const;
    void setCfwdBusyEnabled(bool enabled);
    QString cfwdBusyTarget() const;
    void setCfwdBusyTarget(const QString &uri);
    bool cfwdNoAnswerEnabled() const;
    void setCfwdNoAnswerEnabled(bool enabled);
    QString cfwdNoAnswerTarget() const;
    void setCfwdNoAnswerTarget(const QString &uri);
    int cfwdNoAnswerTimeoutMs() const;
    void setCfwdNoAnswerTimeoutMs(int ms);

signals:
    void callStateChanged();
    void incomingCallChanged();
    void noticeChanged();
    void dialerUriChanged();
    void logLevelChanged();
    void ringtoneEnabledChanged();
    void themeIdChanged();
    void registeredAccountCountChanged();
    void activeAccountIdChanged();
    void voicemailStateChanged();
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
    void enterpriseFeaturesEnabledChanged();
    void crashReportingEnabledChanged();
    void unreadMessageCountChanged();

    // Tray-initiated requests bubbled up to QML.
    void trayShowRequested();
    void trayHideRequested();

    // Generic provisioning lifecycle. providerId tracks which backend the
    // event came from so a wizard can ignore stale signals.
    void provisioningProgress(QString providerId, QString stage);
    void provisioningFailed(QString providerId, QString error);
    void accountProvisioned(QString providerId, int accountId);
    void authMethodsDiscovered(QString providerId, QString host, QVariantList methods);
    void authMethodsFailed(QString providerId, QString host, QString error);

private:
    QString m_notice;
    QTimer m_noticeTimer;

    std::unique_ptr<persistence::Database>      m_db;
    std::unique_ptr<platform::IKeychain>        m_keychain;
    std::unique_ptr<sip::SipEngine>             m_engine;
    std::unique_ptr<sip::AccountsManager>       m_accounts;
    std::unique_ptr<models::AccountsModel>      m_accountsModel;
    std::unique_ptr<sip::CallManager>           m_calls;
    std::unique_ptr<models::CallsModel>         m_callsModel;
    std::unique_ptr<sip::ContactsManager>       m_contacts;
    std::unique_ptr<models::ContactsModel>      m_contactsModel;
    std::unique_ptr<sip::HistoryManager>        m_historyMgr;
    std::unique_ptr<models::HistoryModel>       m_historyModel;
    std::unique_ptr<sip::MessagesManager>       m_messagesMgr;
    std::unique_ptr<models::MessagesModel>      m_messagesModel;
    std::unique_ptr<models::ConversationsModel> m_conversationsModel;
    std::unique_ptr<sip::LinesManager>          m_linesMgr;
    std::unique_ptr<models::LinesModel>         m_linesModel;
    std::unique_ptr<sip::SettingsManager>       m_settings;
    std::unique_ptr<AccountsController>         m_accountsController;
    std::unique_ptr<CallsController>            m_callsController;
    std::unique_ptr<SettingsController>         m_settingsController;
    std::unique_ptr<TrayController>             m_trayController;
    std::unique_ptr<NetworkMonitor>             m_networkMonitor;
    std::unique_ptr<PowerMonitor>               m_powerMonitor;
    std::unique_ptr<UpdateChecker>              m_updateChecker;
    std::unique_ptr<provisioning::Registry>     m_provisioningRegistry;

    QString m_dialerUri;

    void postNotice(const QString &text, int autoDismissMs = 4000);
};

} // namespace compactphone

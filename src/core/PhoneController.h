#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QString>
#include <QVariantMap>
#include <QTimer>
#include <QUrl>
#include <QtQmlIntegration>

#include "ContactsController.h"
#include "LinesController.h"
#include "MessagesController.h"
#include "NoticeDuration.h"
#include "ProvisioningController.h"
#include "SettingsController.h"

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
    Q_PROPERTY(compactphone::SettingsController *settings READ settingsController CONSTANT)
    Q_PROPERTY(QAbstractListModel *accounts READ accountsModel CONSTANT)
    Q_PROPERTY(QAbstractListModel *calls READ callsModel CONSTANT)
    Q_PROPERTY(int incomingCallId READ incomingCallId NOTIFY incomingCallChanged)
    Q_PROPERTY(QString incomingCallFrom READ incomingCallFrom NOTIFY incomingCallChanged)
    Q_PROPERTY(QString notice READ notice NOTIFY noticeChanged)
    Q_PROPERTY(QString latestUpdateVersion READ latestUpdateVersion NOTIFY latestUpdateChanged)
    Q_PROPERTY(QString latestUpdateUrl READ latestUpdateUrl NOTIFY latestUpdateChanged)
    Q_PROPERTY(compactphone::ContactsController *contacts READ contactsController CONSTANT)
    Q_PROPERTY(QAbstractListModel *history READ historyModel CONSTANT)
    Q_PROPERTY(compactphone::MessagesController *messaging READ messagesController CONSTANT)
    Q_PROPERTY(compactphone::LinesController *lines READ linesController CONSTANT)
    Q_PROPERTY(compactphone::ProvisioningController *provisioning READ provisioningController CONSTANT)
    Q_PROPERTY(QString dialerUri READ dialerUri WRITE setDialerUri NOTIFY dialerUriChanged)
    Q_PROPERTY(int registeredAccountCount READ registeredAccountCount NOTIFY registeredAccountCountChanged)
    Q_PROPERTY(int activeAccountId READ activeAccountId WRITE setActiveAccountId NOTIFY activeAccountIdChanged)
    Q_PROPERTY(int newVoicemailCount READ newVoicemailCount NOTIFY voicemailStateChanged)
    Q_PROPERTY(QString activeVoicemailNumber READ activeVoicemailNumber NOTIFY voicemailStateChanged)
    // Settings live on the SettingsController, reachable from QML as
    // PhoneController.settings.<x> — PhoneController no longer re-exports them.
    Q_PROPERTY(bool crashReportingAvailable READ crashReportingAvailable CONSTANT)
    QML_ELEMENT
    QML_SINGLETON
public:
    explicit PhoneController(QObject *parent = nullptr);
    ~PhoneController() override;

    QString callState() const;
    SettingsController *settingsController() const;
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
    QString latestUpdateVersion() const;
    QString latestUpdateUrl() const;
    Q_INVOKABLE bool blindTransfer(int callId, const QString &targetUri);
    Q_INVOKABLE bool attendedTransfer(int activeCallId, int destCallId);
    Q_INVOKABLE bool mergeCalls(int activeCallId, int heldCallId);

    Q_INVOKABLE bool startRecording(int callId);
    Q_INVOKABLE bool stopRecording(int callId);
    Q_INVOKABLE bool isRecording(int callId) const;

    bool crashReportingAvailable() const;

    // Returns the first held confirmed call other than excludeCallId, or
    // -1 if none — used by the UI to enable the Merge button.
    Q_INVOKABLE int firstHeldCallId(int excludeCallId) const;
    Q_INVOKABLE void dismissNotice();

    ContactsController *contactsController() const;
    MessagesController *messagesController() const;
    LinesController *linesController() const;
    ProvisioningController *provisioningController() const;
    QAbstractListModel *historyModel() const;

    QString dialerUri() const { return m_dialerUri; }
    void setDialerUri(const QString &u);

    Q_INVOKABLE QStringList recentLogLines() const;
    Q_INVOKABLE bool exportDiagnostics(const QString &path) const;
    Q_INVOKABLE void checkForUpdates();
    Q_INVOKABLE void openLatestUpdateUrl();
    // Throttled auto-check used at startup; respects the user's setting and
    // their "ignore this version" choice. Safe to call on every launch.
    Q_INVOKABLE void maybeCheckForUpdatesOnStartup();
    // Persist a version the user chose to ignore; the auto-check won't surface
    // it again (newer versions still prompt).
    Q_INVOKABLE void skipUpdateVersion(const QString &version);

    // Live RTCP-derived media stats for the in-call quality indicator.
    // Returns a QVariantMap with keys: mos (double), lossPct (double),
    // rttMs (int), jitterMs (int). Missing/unavailable fields are -1.
    Q_INVOKABLE QVariantMap streamStats(int callId) const;
    Q_INVOKABLE void redialFromHistory(int historyId);

    int registeredAccountCount() const;
    int activeAccountId() const;
    void setActiveAccountId(int id);

    int newVoicemailCount() const;
    QString activeVoicemailNumber() const;
    Q_INVOKABLE void dialVoicemail();

    Q_INVOKABLE void requestShow();
    Q_INVOKABLE void requestQuit();

signals:
    void callStateChanged();
    void incomingCallChanged();
    void noticeChanged();
    void latestUpdateChanged();
    // A newer version is available and should be offered to the user in a
    // modal prompt (Download / Ignore for now / Ignore this version).
    void updatePromptRequested(QString version, QString url);
    void dialerUriChanged();
    void registeredAccountCountChanged();
    void activeAccountIdChanged();
    void voicemailStateChanged();

    // Tray-initiated requests bubbled up to QML.
    void trayShowRequested();
    void trayHideRequested();

private:
    QString m_notice;
    QTimer m_noticeTimer;
    QString m_latestUpdateVersion;
    QUrl m_latestUpdateUrl;
    // True while a startup auto-check is in flight: suppresses the "checking/
    // up-to-date" notices and honours the skipped-version preference. A manual
    // check (Settings button) clears it so the user always gets feedback.
    bool m_autoUpdateCheckActive = false;

    std::unique_ptr<persistence::Database>      m_db;
    std::unique_ptr<platform::IKeychain>        m_keychain;
    std::unique_ptr<sip::SipEngine>             m_engine;
    std::unique_ptr<sip::AccountsManager>       m_accounts;
    std::unique_ptr<models::AccountsModel>      m_accountsModel;
    std::unique_ptr<sip::CallManager>           m_calls;
    std::unique_ptr<models::CallsModel>         m_callsModel;
    std::unique_ptr<sip::ContactsManager>       m_contacts;
    std::unique_ptr<models::ContactsModel>      m_contactsModel;
    std::unique_ptr<ContactsController>         m_contactsController;
    std::unique_ptr<sip::HistoryManager>        m_historyMgr;
    std::unique_ptr<models::HistoryModel>       m_historyModel;
    std::unique_ptr<sip::MessagesManager>       m_messagesMgr;
    std::unique_ptr<models::MessagesModel>      m_messagesModel;
    std::unique_ptr<models::ConversationsModel> m_conversationsModel;
    std::unique_ptr<MessagesController>         m_messagesController;
    std::unique_ptr<sip::LinesManager>          m_linesMgr;
    std::unique_ptr<models::LinesModel>         m_linesModel;
    std::unique_ptr<LinesController>            m_linesController;
    std::unique_ptr<sip::SettingsManager>       m_settings;
    std::unique_ptr<AccountsController>         m_accountsController;
    std::unique_ptr<CallsController>            m_callsController;
    std::unique_ptr<SettingsController>         m_settingsController;
    std::unique_ptr<TrayController>             m_trayController;
    std::unique_ptr<NetworkMonitor>             m_networkMonitor;
    std::unique_ptr<PowerMonitor>               m_powerMonitor;
    std::unique_ptr<UpdateChecker>              m_updateChecker;
    std::unique_ptr<ProvisioningController>     m_provisioningController;

    QString m_dialerUri;

    void postNotice(const QString &text, int autoDismissMs = notice::kDefault);
};

} // namespace compactphone

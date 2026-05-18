#pragma once

#include "CallEntry.h"
#include "CallSessionTracker.h"

#include <QAbstractListModel>
#include <QObject>
#include <QString>

#include <functional>

namespace compactphone::models {
class CallsModel;
class HistoryModel;
}

namespace compactphone::sip {
class AccountsManager;
class CallManager;
class HistoryManager;
}

namespace compactphone {

class SettingsController;

class CallsController : public QObject {
    Q_OBJECT
public:
    using ActiveAccountProvider = std::function<int()>;
    using NoticeSink = std::function<void(const QString &, int)>;

    explicit CallsController(sip::AccountsManager *accounts,
                             sip::CallManager *calls,
                             models::CallsModel *callsModel,
                             sip::HistoryManager *history,
                             models::HistoryModel *historyModel,
                             SettingsController *settings,
                             ActiveAccountProvider activeAccountProvider,
                             NoticeSink noticeSink,
                             QObject *parent = nullptr);
    ~CallsController() override;

    QString callState() const { return m_callState; }
    QAbstractListModel *model() const;
    int incomingCallId() const { return m_incomingCallId; }
    QString incomingCallFrom() const { return m_incomingCallFrom; }
    bool ringing() const;

    void dial(const QString &uri);
    void hangup(int callId);
    bool hold(int callId);
    bool unhold(int callId);
    bool setMuted(int callId, bool muted);
    bool sendDtmf(int callId, const QString &digits);
    bool acceptIncoming();
    bool declineIncoming();
    bool blindTransfer(int callId, const QString &targetUri);
    bool attendedTransfer(int activeCallId, int destCallId);
    bool mergeCalls(int activeCallId, int heldCallId);

    bool startRecording(int callId);
    bool stopRecording(int callId);
    bool isRecording(int callId) const;

signals:
    void callStateChanged();
    void incomingCallChanged();
    void ringingChanged(bool ringing);

private:
    sip::AccountsManager *m_accounts = nullptr;
    sip::CallManager *m_calls = nullptr;
    models::CallsModel *m_callsModel = nullptr;
    sip::HistoryManager *m_history = nullptr;
    models::HistoryModel *m_historyModel = nullptr;
    SettingsController *m_settings = nullptr;
    ActiveAccountProvider m_activeAccountProvider;
    NoticeSink m_noticeSink;

    QString m_callState = QStringLiteral("idle");
    int m_currentCallId = -1;
    int m_incomingCallId = -1;
    QString m_incomingCallFrom;
    bool m_ringing = false;
    sip::CallSessionTracker m_callSessions;

    void refreshCallsModel();
    void publishRingingState();
    void postNotice(const QString &text, int autoDismissMs = 4000);
};

} // namespace compactphone

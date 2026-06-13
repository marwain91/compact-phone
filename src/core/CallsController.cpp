#include "CallsController.h"

#include "AccountsManager.h"
#include "CallManager.h"
#include "HistoryManager.h"
#include "SettingsController.h"
#include "SipUri.h"
#include "models/CallsModel.h"
#include "models/HistoryModel.h"

#include <QDateTime>
#include <QDir>
#include <QMetaObject>
#include <QTimer>

#include <spdlog/spdlog.h>

#include <optional>
#include <utility>

namespace compactphone {

namespace {

QString callStateToString(sip::CallState s)
{
    switch (s) {
    case sip::CallState::Idle:         return QStringLiteral("idle");
    case sip::CallState::Calling:      return QStringLiteral("calling");
    case sip::CallState::EarlyMedia:   return QStringLiteral("early");
    case sip::CallState::Confirmed:    return QStringLiteral("active");
    case sip::CallState::Disconnected: return QStringLiteral("ended");
    }
    return QStringLiteral("unknown");
}

// The helpers below operate on an already-taken snapshot so a single
// CallManager::snapshot() (a PJSIP getInfo() per live call + a heap vector)
// can be shared across one event handler instead of taken three times.
std::optional<sip::CallEntry> findCallEntry(const std::vector<sip::CallEntry> &snap,
                                            sip::CallId id)
{
    for (const auto &entry : snap) {
        if (entry.id == id) return entry;
    }
    return std::nullopt;
}

QString aggregateCallState(const std::vector<sip::CallEntry> &snap,
                           sip::CallState fallback)
{
    bool hasCalling = false;
    bool hasEarly = false;
    for (const auto &entry : snap) {
        if (entry.state == sip::CallState::Confirmed) {
            return QStringLiteral("active");
        }
        hasEarly = hasEarly || entry.state == sip::CallState::EarlyMedia;
        hasCalling = hasCalling || entry.state == sip::CallState::Calling;
    }
    if (hasEarly) return QStringLiteral("early");
    if (hasCalling) return QStringLiteral("calling");
    return callStateToString(fallback);
}

bool snapshotIsRinging(const std::vector<sip::CallEntry> &snap)
{
    for (const auto &e : snap) {
        // Inbound, pre-answer: the user needs to hear the ringtone.
        if (e.direction == sip::CallDirection::Inbound
            && e.state == sip::CallState::Calling) {
            return true;
        }
        // Outbound, while the remote is alerted: local ringback until the
        // remote answers (Confirmed), when PJSIP's audio routing takes over.
        if (e.direction == sip::CallDirection::Outbound
            && (e.state == sip::CallState::Calling
                || e.state == sip::CallState::EarlyMedia)) {
            return true;
        }
    }
    return false;
}

} // namespace

CallsController::CallsController(sip::AccountsManager *accounts,
                                 sip::CallManager *calls,
                                 models::CallsModel *callsModel,
                                 sip::HistoryManager *history,
                                 models::HistoryModel *historyModel,
                                 SettingsController *settings,
                                 ActiveAccountProvider activeAccountProvider,
                                 NoticeSink noticeSink,
                                 QObject *parent)
    : QObject(parent),
      m_accounts(accounts),
      m_calls(calls),
      m_callsModel(callsModel),
      m_history(history),
      m_historyModel(historyModel),
      m_settings(settings),
      m_activeAccountProvider(std::move(activeAccountProvider)),
      m_noticeSink(std::move(noticeSink))
{
    if (m_calls) {
        // The backend wraps incoming calls eagerly inside the PJSIP callback
        // (retaining the INVITE session) and CallManager records them before
        // emitting this signal on the main thread — the old synchronous-
        // adoption constraint is gone (see PjsipBackend::wrapIncomingCall).
        connect(m_calls, &sip::CallManager::incomingCall,
                this, [this](int callId) {
                    if (!m_calls) return;
                    refreshCallsModel();
                    {
                        m_incomingCallId = callId;
                        if (auto e = findCallEntry(m_calls->snapshot(), callId)) {
                            m_callSessions.noteIncoming(
                                *e, QDateTime::currentMSecsSinceEpoch());
                            m_incomingCallFrom =
                                QString::fromStdString(e->remoteUri);
                        }
                        emit incomingCallChanged();

                        // Precedence: DND > forward-always > forward-on-busy
                        // > auto-answer-after-delay > forward-on-no-answer.
                        if (m_settings && m_settings->dndEnabled()) {
                            m_calls->decline(callId);
                            return;
                        }

                        if (m_settings && m_settings->cfwdAlwaysEnabled()) {
                            const QString t = m_settings->cfwdAlwaysTarget().trimmed();
                            if (!t.isEmpty()) {
                                m_calls->forwardCall(callId, t.toStdString());
                                return;
                            }
                        }

                        if (m_settings && m_settings->cfwdBusyEnabled()
                            && m_calls->callCount() > 1) {
                            const QString t = m_settings->cfwdBusyTarget().trimmed();
                            if (!t.isEmpty()) {
                                m_calls->forwardCall(callId, t.toStdString());
                                return;
                            }
                        }

                        if (m_settings && m_settings->autoAnswerEnabled()) {
                            const int delay = m_settings->autoAnswerDelayMs();
                            QTimer::singleShot(delay, this, [this, callId] {
                                if (m_incomingCallId == callId && m_calls) {
                                    m_calls->accept(callId);
                                }
                            });
                        } else if (m_settings && m_settings->cfwdNoAnswerEnabled()) {
                            const QString t = m_settings->cfwdNoAnswerTarget().trimmed();
                            const int timeout = m_settings->cfwdNoAnswerTimeoutMs();
                            if (!t.isEmpty()) {
                                const std::string target = t.toStdString();
                                QTimer::singleShot(timeout, this,
                                    [this, callId, target] {
                                        if (m_incomingCallId == callId && m_calls) {
                                            m_calls->forwardCall(callId, target);
                                        }
                                    });
                            }
                        }
                    }
                    publishRingingState();
                });

        connect(m_calls, &sip::CallManager::callsChanged,
                this, [this] {
                    refreshCallsModel();
                    if (m_calls && m_calls->callCount() == 0 && m_currentCallId >= 0) {
                        m_currentCallId = -1;
                    }
                    publishRingingState();
                });

        connect(m_calls, &sip::CallManager::callEvent, this,
                [this](sip::CallId callId, sip::CallState s) {
            QMetaObject::invokeMethod(this, [this, callId, s] {
                const auto now = QDateTime::currentMSecsSinceEpoch();
                // One snapshot shared across this whole handler.
                const auto snap = m_calls ? m_calls->snapshot()
                                          : std::vector<sip::CallEntry>{};
                sip::CallEntry entry;
                entry.id = callId;
                if (auto found = findCallEntry(snap, callId)) {
                    entry = *found;
                }

                if (auto history = m_callSessions.noteState(entry, s, now)) {
                    if (m_history) m_history->append(*history);
                    if (m_historyModel) m_historyModel->refresh();
                }

                if (s == sip::CallState::Disconnected && callId == m_currentCallId) {
                    m_currentCallId = -1;
                }

                m_callState = aggregateCallState(snap, s);
                refreshCallsModel();
                publishRingingState(snapshotIsRinging(snap));
                if (s == sip::CallState::Disconnected && m_incomingCallId == callId) {
                    m_incomingCallId = -1;
                    m_incomingCallFrom.clear();
                    emit incomingCallChanged();
                }
                // Auto-record when the call confirms. Media must be active
                // for the recorder to bind, so we try once now and once
                // after a short delay (covers re-INVITE timing).
                if (s == sip::CallState::Confirmed && m_settings
                    && m_settings->autoRecordEnabled()) {
                    if (!startRecording(static_cast<int>(callId))) {
                        QTimer::singleShot(500, this, [this, callId] {
                            startRecording(static_cast<int>(callId));
                        });
                    }
                }
                emit callStateChanged();
            }, Qt::QueuedConnection);
        });
    }
}

CallsController::~CallsController()
{
    // All connections to CallManager (incomingCall, callsChanged, callEvent)
    // are severed automatically: CallsController is the connection context
    // object, so they disconnect when it is destroyed.
}

QAbstractListModel *CallsController::model() const
{
    return m_callsModel;
}

bool CallsController::ringing() const
{
    if (!m_calls) return false;
    return snapshotIsRinging(m_calls->snapshot());
}

void CallsController::dial(const QString &uri)
{
    if (!m_calls || uri.trimmed().isEmpty()) return;
    const auto aid = m_activeAccountProvider ? m_activeAccountProvider() : -1;

    // Resolve the calling account so we know which domain + transport to
    // apply when the user typed only an extension. Falls back to the
    // default account when no active is set.
    sip::AccountId resolvedId = aid > 0
        ? static_cast<sip::AccountId>(aid)
        : (m_accounts ? m_accounts->defaultAccountId() : sip::kInvalidAccountId);

    std::string normalizedUri = uri.trimmed().toStdString();
    if (m_accounts && resolvedId != sip::kInvalidAccountId) {
        if (auto acc = m_accounts->find(resolvedId)) {
            normalizedUri = sip::normalizeSipTarget(
                normalizedUri, acc->domain, acc->transport);
        }
    }

    // De-bounce duplicate dials. makeCall() is synchronous and the dialer keeps
    // its Call action live (the number stays in the field), so a burst of Enter
    // presses — natural when the user is unsure the first one registered — would
    // otherwise stack several identical concurrent calls. Ignore a request whose
    // target already has a live outbound call; a different target (second line /
    // attended transfer) and a re-dial after this call ends are still allowed.
    if (m_callSessions.hasLiveOutboundTo(normalizedUri)) {
        spdlog::debug("CallsController::dial: ignoring duplicate dial to {} "
                      "(call already in progress)", normalizedUri);
        return;
    }

    if (aid > 0) {
        m_currentCallId = m_calls->makeCall(static_cast<sip::AccountId>(aid),
                                            normalizedUri);
    } else if (m_accounts) {
        m_currentCallId = m_calls->makeCall(normalizedUri);
    }
    if (m_currentCallId >= 0) {
        m_callSessions.noteOutbound(
            static_cast<sip::CallId>(m_currentCallId),
            resolvedId,
            normalizedUri,
            QDateTime::currentMSecsSinceEpoch());
    }
    refreshCallsModel();
}

void CallsController::hangup(int callId)
{
    if (m_calls && callId >= 0) m_calls->hangup(static_cast<sip::CallId>(callId));
}

bool CallsController::hold(int callId)
{
    if (!m_calls) return false;
    const bool ok = m_calls->hold(static_cast<sip::CallId>(callId));
    if (ok) refreshCallsModel();
    return ok;
}

bool CallsController::unhold(int callId)
{
    if (!m_calls) return false;
    const bool ok = m_calls->unhold(static_cast<sip::CallId>(callId));
    if (ok) refreshCallsModel();
    return ok;
}

bool CallsController::setMuted(int callId, bool muted)
{
    if (!m_calls) return false;
    const bool ok = m_calls->setMuted(static_cast<sip::CallId>(callId), muted);
    if (ok) refreshCallsModel();
    return ok;
}

bool CallsController::sendDtmf(int callId, const QString &digits)
{
    return m_calls
        && m_calls->sendDtmf(static_cast<sip::CallId>(callId),
                             digits.toStdString());
}

bool CallsController::acceptIncoming()
{
    if (!m_calls || m_incomingCallId < 0) return false;
    const bool ok = m_calls->accept(static_cast<sip::CallId>(m_incomingCallId));
    if (ok) {
        m_incomingCallId = -1;
        m_incomingCallFrom.clear();
        emit incomingCallChanged();
        refreshCallsModel();
        publishRingingState();
    }
    return ok;
}

bool CallsController::declineIncoming()
{
    if (!m_calls || m_incomingCallId < 0) return false;
    const bool ok = m_calls->decline(static_cast<sip::CallId>(m_incomingCallId));
    if (ok) {
        m_incomingCallId = -1;
        m_incomingCallFrom.clear();
        emit incomingCallChanged();
        refreshCallsModel();
        publishRingingState();
    }
    return ok;
}

bool CallsController::blindTransfer(int callId, const QString &targetUri)
{
    if (!m_calls) return false;
    QString normalizedTarget = targetUri;
    if (auto call = findCallEntry(m_calls->snapshot(), static_cast<sip::CallId>(callId))) {
        if (m_accounts) {
            if (auto account = m_accounts->find(call->accountId)) {
                normalizedTarget = QString::fromStdString(sip::normalizeSipTarget(
                    targetUri.toStdString(), account->domain, account->transport));
            }
        }
    }
    const bool ok = m_calls->blindTransfer(static_cast<sip::CallId>(callId),
                                           normalizedTarget.toStdString());
    postNotice(ok ? QStringLiteral("Transferring...")
                  : QStringLiteral("Transfer failed"));
    return ok;
}

bool CallsController::attendedTransfer(int activeCallId, int destCallId)
{
    if (!m_calls) return false;
    const bool ok = m_calls->attendedTransfer(
        static_cast<sip::CallId>(activeCallId),
        static_cast<sip::CallId>(destCallId));
    postNotice(ok ? QStringLiteral("Transferring...")
                  : QStringLiteral("Transfer failed"));
    return ok;
}

bool CallsController::mergeCalls(int activeCallId, int heldCallId)
{
    if (!m_calls) return false;
    const bool ok = m_calls->mergeCalls(
        static_cast<sip::CallId>(activeCallId),
        static_cast<sip::CallId>(heldCallId));
    postNotice(ok ? QStringLiteral("Calls merged")
                  : QStringLiteral("Could not merge calls"));
    return ok;
}

bool CallsController::startRecording(int callId)
{
    if (!m_calls || !m_settings) return false;
    const QString dir = m_settings->recordingsPath();
    if (dir.isEmpty()) return false;
    QDir().mkpath(dir);
    const QString filename = QStringLiteral("call_%1_%2.wav")
        .arg(callId)
        .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss"));
    const QString full = dir + QStringLiteral("/") + filename;
    const bool ok = m_calls->startRecording(
        static_cast<sip::CallId>(callId), full.toStdString());
    if (ok) postNotice(QStringLiteral("Recording started"));
    return ok;
}

bool CallsController::stopRecording(int callId)
{
    if (!m_calls) return false;
    const bool ok = m_calls->stopRecording(static_cast<sip::CallId>(callId));
    if (ok) postNotice(QStringLiteral("Recording saved"));
    return ok;
}

bool CallsController::isRecording(int callId) const
{
    return m_calls
        && m_calls->isRecording(static_cast<sip::CallId>(callId));
}

void CallsController::refreshCallsModel()
{
    if (m_callsModel) m_callsModel->refresh();
}

void CallsController::publishRingingState()
{
    if (!m_calls) { publishRingingState(false); return; }
    publishRingingState(snapshotIsRinging(m_calls->snapshot()));
}

void CallsController::publishRingingState(bool now)
{
    if (now == m_ringing) return;
    m_ringing = now;
    emit ringingChanged(now);
}

void CallsController::postNotice(const QString &text, int autoDismissMs)
{
    if (m_noticeSink) m_noticeSink(text, autoDismissMs);
}

} // namespace compactphone

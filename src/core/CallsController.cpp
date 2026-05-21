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

std::optional<sip::CallEntry> findCallEntry(sip::CallManager *calls,
                                            sip::CallId id)
{
    if (!calls) return std::nullopt;
    for (const auto &entry : calls->snapshot()) {
        if (entry.id == id) return entry;
    }
    return std::nullopt;
}

QString aggregateCallState(sip::CallManager *calls, sip::CallState fallback)
{
    if (!calls) return callStateToString(fallback);
    bool hasCalling = false;
    bool hasEarly = false;
    for (const auto &entry : calls->snapshot()) {
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
    if (m_accounts) {
        m_accounts->setOnIncomingCall(
            [this](sip::AccountId aid, int pjsipCallId) {
                // Adoption MUST happen synchronously on the PJSIP thread.
                // If we defer it to the main thread, PJSIP's INVITE
                // session is gone by the time we wrap the call object
                // (it tears down within ~15ms when nothing claims it),
                // so subsequent accept/decline calls fail with
                // PJSIP_ESESSIONTERMINATED.
                if (!m_calls) return;
                const auto callId = m_calls->adoptIncomingCall(aid, pjsipCallId);
                if (callId == sip::kInvalidCallId) return;

                QMetaObject::invokeMethod(this, [this, callId] {
                    if (!m_calls) return;
                    refreshCallsModel();
                    {
                        m_incomingCallId = callId;
                        if (auto e = findCallEntry(m_calls, callId)) {
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
                }, Qt::QueuedConnection);
            });
    }

    if (m_calls) {
        connect(m_calls, &sip::CallManager::callsChanged,
                this, [this] {
                    refreshCallsModel();
                    if (m_calls && m_calls->callCount() == 0 && m_currentCallId >= 0) {
                        m_currentCallId = -1;
                    }
                    publishRingingState();
                });

        m_calls->setOnCallEvent([this](sip::CallId callId, sip::CallState s) {
            QMetaObject::invokeMethod(this, [this, callId, s] {
                const auto now = QDateTime::currentMSecsSinceEpoch();
                sip::CallEntry entry;
                entry.id = callId;
                if (auto found = findCallEntry(m_calls, callId)) {
                    entry = *found;
                }

                if (auto history = m_callSessions.noteState(entry, s, now)) {
                    if (m_history) m_history->append(*history);
                    if (m_historyModel) m_historyModel->refresh();
                }

                if (s == sip::CallState::Disconnected && callId == m_currentCallId) {
                    m_currentCallId = -1;
                }

                m_callState = aggregateCallState(m_calls, s);
                refreshCallsModel();
                publishRingingState();
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
    if (m_accounts) m_accounts->setOnIncomingCall({});
    if (m_calls) m_calls->setOnCallEvent({});
}

QAbstractListModel *CallsController::model() const
{
    return m_callsModel;
}

bool CallsController::ringing() const
{
    if (!m_calls) return false;
    for (const auto &e : m_calls->snapshot()) {
        // Inbound, pre-answer: the user needs to hear the ringtone so
        // they know to pick up.
        if (e.direction == sip::CallDirection::Inbound
            && e.state == sip::CallState::Calling) {
            return true;
        }
        // Outbound, while the remote is being alerted: play local ringback
        // so the caller doesn't sit in silence. Once the remote answers
        // (state moves to Confirmed) PJSIP's audio routing takes over and
        // we stop the local tone.
        if (e.direction == sip::CallDirection::Outbound
            && (e.state == sip::CallState::Calling
                || e.state == sip::CallState::EarlyMedia)) {
            return true;
        }
    }
    return false;
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
    if (auto call = findCallEntry(m_calls, static_cast<sip::CallId>(callId))) {
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
    const bool now = ringing();
    if (now == m_ringing) return;
    m_ringing = now;
    emit ringingChanged(now);
}

void CallsController::postNotice(const QString &text, int autoDismissMs)
{
    if (m_noticeSink) m_noticeSink(text, autoDismissMs);
}

} // namespace compactphone

#include "CallManager.h"
#include "AccountsManager.h"
#include "CallEntry.h"

#include <spdlog/spdlog.h>

#include <QTimer>

#include <optional>
#include <utility>

namespace compactphone::sip {

CallManager::CallManager(sipbackend::ISipBackend *backend, AccountsManager *am,
                         QObject *parent)
    : QObject(parent), m_backend(backend), m_am(am) {}

CallManager::~CallManager()
{
    // Release every live backend call (the adapter parks/hangs it up). No
    // backend query needed — the records carry everything snapshot() shows.
    for (auto &kv : m_records) m_backend->releaseCall(kv.first);
    m_records.clear();
    m_lingeringCalls.clear();
}

// ---------------------------------------------------------------------------
// Outbound + incoming call setup
// ---------------------------------------------------------------------------

CallId CallManager::makeCall(const std::string &uri)
{
    if (!m_am) return kInvalidCallId;
    return makeCall(m_am->defaultAccountId(), uri);
}

CallId CallManager::makeCall(AccountId accountId, const std::string &uri)
{
    if (!m_am) return kInvalidCallId;
    const auto backendAcc = m_am->backendIdFor(accountId);
    if (backendAcc == sipbackend::kInvalidAccountId) {
        spdlog::error("CallManager::makeCall: account {} not registered",
                      accountId);
        return kInvalidCallId;
    }
    const CallId id = m_backend->makeCall(backendAcc, uri);
    if (id == sipbackend::kInvalidCallId) return kInvalidCallId;
    CallRecord r;
    r.accountId = accountId;
    r.remoteUri = uri;          // push model: we dialed it, we know it
    r.inbound = false;
    r.state = CallState::Calling;
    m_records.emplace(id, std::move(r));
    setActiveCall(id);
    return id;
}

void CallManager::onIncomingCall(sipbackend::AccountId backendAccId,
                                 sipbackend::CallId callId,
                                 const std::string &remoteUri,
                                 const std::string &displayName)
{
    const auto accountId = m_am ? m_am->accountIdForBackend(backendAccId)
                                : kInvalidAccountId;
    if (accountId == kInvalidAccountId) {
        // Account removed between the adapter's wrap and this delivery.
        spdlog::warn("CallManager: incoming call {} for unknown backend "
                     "account {} — declining 480", callId, backendAccId);
        m_backend->decline(callId, 480);
        m_backend->releaseCall(callId);
        return;
    }
    CallRecord r;
    r.accountId = accountId;
    r.remoteUri = remoteUri;
    r.remoteDisplayName = displayName;
    r.inbound = true;
    r.state = CallState::Calling;
    m_records.emplace(callId, std::move(r));
    notifyStateChange(callId, CallState::Calling);
    emit callsChanged();
    emit incomingCall(static_cast<int>(callId));
}

void CallManager::onCallState(sipbackend::CallId id, sipbackend::CallState s,
                              int sipCode)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return;   // released/unknown id: tolerated
    // sip::CallState mirrors sipbackend::CallState value-for-value
    // (static-asserted in tests/unit/test_sipbackend_types.cpp).
    const auto state = static_cast<CallState>(s);
    if (state == CallState::Disconnected) {
        // Record the code BEFORE notifying, preserving the guarantee that
        // an observer seeing Disconnected can immediately read
        // lastStatusCode().
        it->second.lastStatusCode = sipCode;
        it->second.state = state;
        notifyStateChange(id, state);
        handleDisconnected(id, sipCode);
        return;
    }
    it->second.state = state;
    notifyStateChange(id, state);
}

void CallManager::handleDisconnected(CallId id, int /*sipCode*/)
{
    // Freeze the lingering UI snapshot from our own record — no backend
    // query (push model; this is what deleted the URI caches), then
    // release the backend id. The adapter parks the pj::Call internally
    // (grace dance, now adapter-private).
    auto it = m_records.find(id);
    if (it == m_records.end()) return;
    CallRecord lingering = it->second;
    lingering.state = CallState::Disconnected;
    m_records.erase(it);
    m_transfers.drop(id);
    m_backend->releaseCall(id);
    m_lingeringCalls[id] = std::move(lingering);

    if (id == m_activeCallId) {
        // Promote a held confirmed call to active.
        m_activeCallId = kInvalidCallId;
        for (auto &kv : m_records) {
            if (!kv.second.held || kv.second.state != CallState::Confirmed)
                continue;
            m_activeCallId = kv.first;
            requestUnhold(kv.first, 3);
            break;
        }
    }
    emit callsChanged();
    QTimer::singleShot(m_lingerMs, this, [this, id] { eraseLingering(id); });
}

void CallManager::eraseLingering(CallId id)
{
    if (m_lingeringCalls.erase(id) > 0) emit callsChanged();
}

// ---------------------------------------------------------------------------
// Accept / decline / forward / active-call promotion
// ---------------------------------------------------------------------------

bool CallManager::accept(CallId id)
{
    if (m_records.find(id) == m_records.end()) return false;
    // Promote to active first — auto-holds whichever call was active.
    setActiveCall(id);
    return m_backend->answer(id);
}

bool CallManager::decline(CallId id)
{
    if (m_records.find(id) == m_records.end()) return false;
    // 486 Busy Here, not 603 Decline: 486 invites the server to apply its
    // busy treatment (forward-on-busy, voicemail, hunt), which is what a
    // declined caller should get — DND included. 603 means "do not try to
    // reach me anywhere else", and Asterisk additionally relayed it to the
    // caller as a hostile-looking 403 Forbidden (cause-21 mapping); 486
    // passes through as 486.
    return m_backend->decline(id, 486);
}

bool CallManager::forwardCall(CallId id, const std::string &targetUri)
{
    if (m_records.find(id) == m_records.end()) return false;
    if (targetUri.empty()) return false;
    const bool ok = m_backend->redirect(id, targetUri);
    if (ok) spdlog::info("CallManager: forwarded call {} to {}", id, targetUri);
    return ok;
}

void CallManager::setActiveCall(CallId id)
{
    if (m_activeCallId == id) return;
    // Hold the previous active call (if any, confirmed, and not already
    // held). Bug-for-bug with the pre-rewrite impl: the held flag is set
    // even if the backend hold() fails.
    const CallId previousId = m_activeCallId;
    if (previousId != kInvalidCallId) {
        auto pit = m_records.find(previousId);
        if (pit != m_records.end()
            && pit->second.state == CallState::Confirmed
            && !pit->second.held) {
            m_backend->hold(previousId);   // result ignored (bug-for-bug)
            pit->second.held = true;
        }
    }
    m_activeCallId = id;
    // Unhold the new active call if it was held.
    if (id != kInvalidCallId) {
        auto it = m_records.find(id);
        if (it != m_records.end() && it->second.held) {
            m_backend->unhold(id);   // result ignored
            it->second.held = false;
        }
    }
}

// ---------------------------------------------------------------------------
// Hold / unhold (with unhold retry + promote)
// ---------------------------------------------------------------------------

bool CallManager::hold(CallId id)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (!m_backend->hold(id)) return false;
    it->second.held = true;
    return true;
}

bool CallManager::unhold(CallId id)
{
    return requestUnhold(id, 5);
}

bool CallManager::requestUnhold(CallId id, int retriesRemaining)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (!m_backend->unhold(id)) {
        if (retriesRemaining > 0) {
            spdlog::warn("CallManager::unhold deferred; {} retries left",
                         retriesRemaining);
            QTimer::singleShot(300, this, [this, id, retriesRemaining]() {
                requestUnhold(id, retriesRemaining - 1);
            });
            it->second.held = false;
            return true;
        }
        spdlog::error("CallManager::unhold: backend refused");
        return false;
    }
    it->second.held = false;
    // Promote to active, holding whichever call was active.
    if (m_activeCallId != id) {
        const CallId previousId = m_activeCallId;
        if (previousId != kInvalidCallId) {
            auto pit = m_records.find(previousId);
            if (pit != m_records.end()
                && pit->second.state == CallState::Confirmed
                && !pit->second.held) {
                m_backend->hold(previousId);
                pit->second.held = true;
            }
        }
        m_activeCallId = id;
    }
    return true;
}

bool CallManager::isHeld(CallId id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() && it->second.held;
}

// ---------------------------------------------------------------------------
// Conference (merge two confirmed calls)
// ---------------------------------------------------------------------------

bool CallManager::mergeCalls(CallId activeCallId, CallId heldCallId)
{
    if (m_records.find(activeCallId) == m_records.end()
        || m_records.find(heldCallId) == m_records.end()) {
        return false;
    }
    if (!isConfirmedState(activeCallId) || !isConfirmedState(heldCallId)) {
        spdlog::warn("CallManager::mergeCalls: both calls must be confirmed");
        return false;
    }

    if (isHeld(heldCallId)) {
        if (!requestUnhold(heldCallId, 5)) {
            return false;
        }
        QTimer::singleShot(600, this, [this, activeCallId, heldCallId] {
            wireBridge(activeCallId, heldCallId, 5);
        });
        return true;
    }

    return wireBridge(activeCallId, heldCallId, 5);
}

bool CallManager::wireBridge(CallId activeCallId, CallId heldCallId,
                             int retriesRemaining)
{
    if (m_records.find(activeCallId) == m_records.end()
        || m_records.find(heldCallId) == m_records.end()) {
        return false;
    }
    if (m_backend->bridge(activeCallId, heldCallId)) {
        spdlog::info("CallManager: merged calls {} and {} into conference",
                     activeCallId, heldCallId);
        return true;
    }
    // The backend returns false when either leg lacks active audio yet (the
    // re-INVITE has not renegotiated) — retry on the audio-clock timescale.
    if (retriesRemaining <= 0) {
        spdlog::warn("CallManager::mergeCalls: bridge did not succeed");
        return false;
    }
    QTimer::singleShot(400, this,
                       [this, activeCallId, heldCallId, retriesRemaining] {
        wireBridge(activeCallId, heldCallId, retriesRemaining - 1);
    });
    return true;
}

// ---------------------------------------------------------------------------
// Recording / file playback (bookkeeping; mechanics live in the adapter)
// ---------------------------------------------------------------------------

bool CallManager::startRecording(CallId id, const std::string &outputPath)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (!m_backend->startRecording(id, outputPath)) return false;
    it->second.recording = true;
    return true;
}

bool CallManager::stopRecording(CallId id)
{
    if (!m_backend->stopRecording(id)) return false;
    auto it = m_records.find(id);
    if (it != m_records.end()) it->second.recording = false;
    return true;
}

bool CallManager::isRecording(CallId id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() && it->second.recording;
}

bool CallManager::playAudioFile(CallId id, const std::string &path, bool loop)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    if (!m_backend->playFile(id, path, loop)) return false;
    it->second.playingFile = true;
    return true;
}

bool CallManager::stopAudioFile(CallId id)
{
    if (!m_backend->stopFile(id)) return false;
    auto it = m_records.find(id);
    if (it != m_records.end()) it->second.playingFile = false;
    return true;
}

bool CallManager::isPlayingAudioFile(CallId id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() && it->second.playingFile;
}

// ---------------------------------------------------------------------------
// Transfers
// ---------------------------------------------------------------------------

bool CallManager::blindTransfer(CallId id, const std::string &targetUri)
{
    if (m_records.find(id) == m_records.end()) return false;
    m_transfers.record(id, {id});
    if (!m_backend->blindTransfer(id, targetUri)) {
        m_transfers.drop(id);
        return false;
    }
    spdlog::info("Blind transfer of call {} to {}", id, targetUri);
    return true;
}

bool CallManager::attendedTransfer(CallId activeCallId, CallId destCallId)
{
    if (activeCallId == destCallId) return false;
    if (m_records.find(activeCallId) == m_records.end()
        || m_records.find(destCallId) == m_records.end()) {
        return false;
    }
    m_transfers.record(activeCallId, {activeCallId, destCallId});
    if (!m_backend->attendedTransfer(activeCallId, destCallId)) {
        m_transfers.drop(activeCallId);
        return false;
    }
    spdlog::info("Attended transfer of {} to {}", activeCallId, destCallId);
    return true;
}

void CallManager::onTransferStatus(sipbackend::CallId id, int sipCode,
                                   bool isFinal, const std::string &reason)
{
    handleTransferStatus(id, sipCode, isFinal, reason);
}

void CallManager::handleTransferStatus(CallId id, int statusCode,
                                       bool finalNotify,
                                       const std::string &reason)
{
    // Always runs on the main thread (queued listener event). Which legs to
    // hang up is decided (and unit-tested) in TransferTracker; this method
    // only performs the hangups.
    const bool wasPending = m_transfers.has(id);
    const auto cleanupIds = m_transfers.takeLegsToHangup(id, statusCode,
                                                         finalNotify);
    if (cleanupIds.empty()) {
        if (wasPending && finalNotify) {
            spdlog::warn("Transfer of call {} finished with {} {}", id,
                         statusCode, reason);
        }
        return;
    }
    hangupTransferLegs(cleanupIds);
}

void CallManager::hangupTransferLegs(const std::vector<CallId> &cleanupIds)
{
    // Post-answer hangup is a BYE. Transfer legs are confirmed by the time a
    // transfer completes, so the adapter's SC_DECLINE differs from the old
    // SC_OK only pre-answer — irrelevant here.
    for (const auto cleanupId : cleanupIds) {
        if (m_records.find(cleanupId) == m_records.end()) continue;
        m_backend->hangup(cleanupId);
    }
}

// ---------------------------------------------------------------------------
// Hangup / mute / DTMF
// ---------------------------------------------------------------------------

void CallManager::hangup(CallId id)
{
    if (m_records.find(id) == m_records.end()) return;
    m_backend->hangup(id);
}

bool CallManager::setMuted(CallId id, bool muted)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    // Defer-when-no-active-media semantics live in the adapter now: it
    // records the desired mute state and reports success, applying it when
    // media (re)activates.
    if (!m_backend->setMuted(id, muted)) return false;
    it->second.muted = muted;
    return true;
}

bool CallManager::isMuted(CallId id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() && it->second.muted;
}

bool CallManager::sendDtmf(CallId id, const std::string &digits)
{
    auto it = m_records.find(id);
    if (it == m_records.end()) return false;
    const auto account = m_am ? m_am->find(it->second.accountId)
                              : std::nullopt;
    const auto method = account ? account->dtmfMethod : DtmfMethod::Rfc2833;
    // sip::DtmfMethod mirrors sipbackend::DtmfMethod value-for-value
    // (static-asserted in tests/unit/test_sipbackend_types.cpp).
    return m_backend->sendDtmf(
        id, digits, static_cast<sipbackend::DtmfMethod>(
                        static_cast<int>(method)));
}

// ---------------------------------------------------------------------------
// Reads — all delegate to the backend or the local records
// ---------------------------------------------------------------------------

CallManager::StreamStats CallManager::streamStats(CallId id) const
{
    const auto s = m_backend->streamStats(id);
    StreamStats out;
    out.mos = s.mos;
    out.lossPct = s.lossPct;
    out.rttMs = s.rttMs;
    out.jitterMs = s.jitterMs;
    return out;
}

size_t CallManager::callCount() const
{
    return m_records.size();
}

bool CallManager::isCaptureTransmitting(CallId id) const
{
    return m_backend->isCaptureTransmitting(id);
}

bool CallManager::isMediaActive(CallId id) const
{
    return m_backend->isMediaActive(id);
}

int CallManager::lastStatusCode(CallId id) const
{
    // Readable through the linger window: live records first, then the
    // frozen lingering snapshot (strictly more available than the old impl,
    // which dropped the code at grace-erase).
    auto it = m_records.find(id);
    if (it != m_records.end()) return it->second.lastStatusCode;
    auto lit = m_lingeringCalls.find(id);
    if (lit != m_lingeringCalls.end()) return lit->second.lastStatusCode;
    return 0;
}

bool CallManager::isConfirmedState(CallId id) const
{
    auto it = m_records.find(id);
    return it != m_records.end() && it->second.state == CallState::Confirmed;
}

std::vector<CallEntry> CallManager::snapshot() const
{
    // Pure walk over the local records — no backend query, no caches. The
    // push model means everything the UI shows (identity, state, flags) is
    // already in the record.
    std::vector<CallEntry> out;
    out.reserve(m_records.size() + m_lingeringCalls.size());
    for (const auto &[id, r] : m_records) {
        CallEntry e;
        e.id = id;
        e.accountId = r.accountId;
        e.remoteUri = r.remoteUri;
        e.remoteDisplayName = r.remoteDisplayName;
        e.state = r.state;
        e.held = r.held;
        e.muted = r.muted;
        e.recording = r.recording;
        e.direction = r.inbound ? CallDirection::Inbound
                                : CallDirection::Outbound;
        out.push_back(std::move(e));
    }
    for (const auto &[id, r] : m_lingeringCalls) {
        CallEntry e;
        e.id = id;
        e.accountId = r.accountId;
        e.remoteUri = r.remoteUri;
        e.remoteDisplayName = r.remoteDisplayName;
        e.state = r.state;
        e.held = r.held;
        e.muted = r.muted;
        e.recording = r.recording;
        e.direction = r.inbound ? CallDirection::Inbound
                                : CallDirection::Outbound;
        out.push_back(std::move(e));
    }
    return out;
}

// ---------------------------------------------------------------------------
// Callback slots
// ---------------------------------------------------------------------------

void CallManager::setOnCallStateChanged(std::function<void(CallState)> cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_cb = std::move(cb);
}

void CallManager::setOnCallEvent(std::function<void(CallId, CallState)> cb)
{
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    m_eventCb = std::move(cb);
}

void CallManager::notifyStateChange(CallId id, CallState s)
{
    // Main thread (queued listener event). See m_callbackMutex's comment
    // for why the lock survives until phase 4.
    std::lock_guard<std::mutex> lock(m_callbackMutex);
    if (m_cb) m_cb(s);
    if (m_eventCb) m_eventCb(id, s);
}

} // namespace compactphone::sip

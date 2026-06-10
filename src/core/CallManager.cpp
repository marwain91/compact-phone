#include "CallManager.h"
#include "AccountsManager.h"
#include "CallEntry.h"
#include "CallRecorder.h"

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

#include <QMetaObject>
#include <QTimer>

#include <optional>

namespace compactphone::sip {

class CallImpl : public pj::Call {
public:
    CallImpl(pj::Account &acc, CallManager *owner, CallId id,
             AccountId accountId,
             int sysCallId = PJSUA_INVALID_ID)
        : pj::Call(acc, sysCallId),
          m_owner(owner), m_localId(id), m_accountId(accountId) {}

    void onCallState(pj::OnCallStateParam &prm) override
    {
        auto info = getInfo();
        spdlog::info("Call {} state {} -> {}", m_localId,
                     static_cast<int>(info.state), info.stateText);
        switch (info.state) {
        case PJSIP_INV_STATE_CALLING:
        case PJSIP_INV_STATE_INCOMING:
            m_owner->notifyStateChange(m_localId, CallState::Calling); break;
        case PJSIP_INV_STATE_EARLY:
            m_owner->notifyStateChange(m_localId, CallState::EarlyMedia); break;
        case PJSIP_INV_STATE_CONFIRMED:
            m_owner->notifyStateChange(m_localId, CallState::Confirmed); break;
        case PJSIP_INV_STATE_DISCONNECTED:
            m_owner->notifyStateChange(m_localId, CallState::Disconnected); break;
        default: break;
        }

        if (info.state == PJSIP_INV_STATE_DISCONNECTED) {
            const int callId = m_localId;
            CallManager *owner = m_owner;
            QMetaObject::invokeMethod(owner, [owner, callId]() {
                owner->releaseCallToGrace(callId);
            }, Qt::QueuedConnection);
        }
    }

    void onCallMediaState(pj::OnCallMediaStateParam &prm) override
    {
        auto info = getInfo();
        // Media (re)activates not just on call setup but on every re-INVITE —
        // hold/unhold, peer-initiated renegotiation, codec changes. Honour
        // the recorded mute state when wiring capture, or a re-INVITE would
        // silently re-open the microphone on a muted call (and a mute pressed
        // before media became active would never be applied).
        const bool muted = m_owner->isMuted(m_localId);
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;

            auto *aud = static_cast<pj::AudioMedia *>(getMedia(i));
            if (!aud) continue;

            auto &mgr = pj::Endpoint::instance().audDevManager();
            try {
                aud->startTransmit(mgr.getPlaybackDevMedia());
                if (!muted) {
                    mgr.getCaptureDevMedia().startTransmit(*aud);
                } else {
                    // Defensive: drop any capture link PJSIP carried across
                    // the renegotiation.
                    try {
                        mgr.getCaptureDevMedia().stopTransmit(*aud);
                    } catch (const pj::Error &) {}
                }
            } catch (const pj::Error &e) {
                spdlog::error("audio wiring error: {}", e.info());
            }
        }
    }

    void onCallTransferStatus(pj::OnCallTransferStatusParam &prm) override
    {
        spdlog::info("Call {} transfer status {} {} final={}", m_localId,
                     static_cast<int>(prm.statusCode), prm.reason,
                     prm.finalNotify);
        // Marshal to the main thread: the handler walks main-thread-only
        // transfer bookkeeping and hangs up calls (pj::Call::hangup re-enters
        // onCallState on the calling thread). Unlike incoming-call adoption,
        // nothing here is time-critical — the transfer outcome is already
        // decided by the final NOTIFY; the post-transfer hangup tolerates an
        // event-loop tick of latency.
        const CallId callId = m_localId;
        const int statusCode = static_cast<int>(prm.statusCode);
        const bool finalNotify = prm.finalNotify;
        const std::string reason = prm.reason;
        CallManager *owner = m_owner;
        QMetaObject::invokeMethod(owner,
            [owner, callId, statusCode, finalNotify, reason]() {
                owner->handleTransferStatus(callId, statusCode, finalNotify,
                                            reason);
            }, Qt::QueuedConnection);
        if (prm.finalNotify && prm.statusCode >= 200 &&
            prm.statusCode < 300) {
            prm.cont = false;
        }
    }

    CallId localId() const { return m_localId; }
    AccountId accountId() const { return m_accountId; }

private:
    CallManager *m_owner;
    CallId m_localId;
    AccountId m_accountId;
};

CallManager::CallManager(AccountsManager *am, QObject *parent)
    : QObject(parent), m_am(am),
      m_recorder(std::make_unique<CallRecorder>()) {}
CallManager::~CallManager() = default;

size_t CallManager::callCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_calls.size();
}

bool CallManager::isCaptureTransmitting(CallId id) const
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end() || !it->second) return false;
        call = it->second.get();
    }
    try {
        const auto info = call->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;
            auto *aud = static_cast<pj::AudioMedia *>(call->getMedia(i));
            if (!aud) continue;
            const int callPort = aud->getPortId();
            // The capture device's conference-bridge listeners are the ports
            // it transmits to; the mic is live for this call iff the call's
            // media port is among them.
            const auto capInfo = pj::Endpoint::instance().audDevManager()
                                     .getCaptureDevMedia().getPortInfo();
            for (const int listener : capInfo.listeners) {
                if (listener == callPort) return true;
            }
        }
    } catch (const pj::Error &) {
        // No live media — not transmitting.
    }
    return false;
}

CallManager::StreamStats CallManager::streamStats(CallId id) const
{
    StreamStats out;
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end() || !it->second) return out;
        call = it->second.get();
    }

    try {
        pj::CallInfo info = call->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;

            pj::StreamStat s = call->getStreamStat(i);
            const auto &rx = s.rtcp.rxStat;
            const auto &tx = s.rtcp.txStat;
            // Loss: combined Rx+Tx packet loss percentage.
            const long rxTotal = rx.pkt + rx.loss;
            const long txTotal = tx.pkt + tx.loss;
            const long combinedLost = rx.loss + tx.loss;
            const long combinedTotal = rxTotal + txTotal;
            if (combinedTotal > 0) {
                out.lossPct =
                    100.0 * static_cast<double>(combinedLost) / combinedTotal;
            }
            out.jitterMs = static_cast<int>(rx.jitterUsec.mean / 1000);
            out.rttMs = static_cast<int>(s.rtcp.rttUsec.mean / 1000);
            // Crude E-model MOS estimate using R-factor: start at 93
            // (typical clean PSTN), subtract for loss and jitter.
            double r = 93.2
                - (out.lossPct > 0 ? out.lossPct * 2.5 : 0)
                - (out.jitterMs > 30 ? (out.jitterMs - 30) * 0.4 : 0)
                - (out.rttMs > 150 ? (out.rttMs - 150) * 0.024 : 0);
            if (r < 0) r = 0;
            if (r > 100) r = 100;
            out.mos = 1.0 + 0.035 * r + 7e-6 * r * (r - 60) * (100 - r);
            break;
        }
    } catch (const pj::Error &) {
        // No live media yet — return the defaulted -1s.
    }
    return out;
}

void CallManager::releaseCallToGrace(int callId)
{
    const auto id = static_cast<CallId>(callId);
    CallImpl *call = nullptr;
    LingeringCallSnapshot lingering;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return;
        call = it->second.get();
        auto acctIt = m_callAccount.find(id);
        lingering.accountId = acctIt != m_callAccount.end()
                                  ? acctIt->second
                                  : kInvalidAccountId;
    }

    lingering.held = isHeld(id);
    lingering.muted = isMuted(id);
    lingering.recording = isRecording(id);
    try {
        const auto info = call->getInfo();
        if (!info.remoteUri.empty()) {
            m_remoteUriCache[id] = info.remoteUri;
        }
        if (!info.remoteContact.empty()) {
            m_remoteDisplayCache[id] = info.remoteContact;
        }
        const auto cachedUri = m_remoteUriCache.find(id);
        const auto cachedDisplay = m_remoteDisplayCache.find(id);
        lingering.remoteUri = !info.remoteUri.empty()
                                  ? info.remoteUri
                                  : (cachedUri != m_remoteUriCache.end()
                                         ? cachedUri->second
                                         : std::string{});
        lingering.remoteDisplayName =
            !info.remoteContact.empty()
                ? info.remoteContact
                : (cachedDisplay != m_remoteDisplayCache.end()
                       ? cachedDisplay->second
                       : std::string{});
        lingering.inbound = info.role == PJSIP_ROLE_UAS;
    } catch (...) {
        const auto cachedUri = m_remoteUriCache.find(id);
        const auto cachedDisplay = m_remoteDisplayCache.find(id);
        lingering.remoteUri = cachedUri != m_remoteUriCache.end()
                                  ? cachedUri->second
                                  : std::string{};
        lingering.remoteDisplayName = cachedDisplay != m_remoteDisplayCache.end()
                                          ? cachedDisplay->second
                                          : std::string{};
    }

    std::unique_ptr<CallImpl> dying;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it != m_calls.end()) {
            dying = std::move(it->second);
            m_calls.erase(it);
        }
        m_callAccount.erase(id);
        m_mutedState.erase(id);
    }
    // pj::Call's destructor calls into PJSIP — run it outside the lock.
    dying.reset();

    m_heldState.erase(id);
    m_transfers.drop(id);
    m_remoteUriCache.erase(id);
    m_remoteDisplayCache.erase(id);
    m_recorder->drop(id);
    m_players.erase(id);
    m_lingeringCalls[id] = std::move(lingering);

    if (id == m_activeCallId) {
        m_activeCallId = kInvalidCallId;
        for (auto &kv : m_heldState) {
            if (!kv.second) continue;
            bool present = false;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                present = m_calls.count(kv.first) > 0;
            }
            if (present && isConfirmedState(kv.first)) {
                m_activeCallId = kv.first;
                requestUnhold(kv.first, 3);
                break;
            }
        }
    }

    emit callsChanged();
    QTimer::singleShot(2200, this, [this, id]() {
        eraseCall(id);
    });
}

void CallManager::eraseCall(int callId)
{
    const auto id = static_cast<CallId>(callId);
    bool present = m_lingeringCalls.count(id) > 0;
    std::unique_ptr<CallImpl> dying;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it != m_calls.end()) {
            present = true;
            dying = std::move(it->second);
            m_calls.erase(it);
        }
        m_callAccount.erase(id);
        m_callStates.erase(id);
        m_mutedState.erase(id);
    }
    // pj::Call's destructor calls into PJSIP — run it outside the lock.
    dying.reset();

    m_lingeringCalls.erase(id);
    m_heldState.erase(id);
    m_transfers.drop(id);
    m_remoteUriCache.erase(id);
    m_remoteDisplayCache.erase(id);
    m_recorder->drop(id);   // closes the WAV file if still recording
    m_players.erase(id);
    if (present) emit callsChanged();

    if (id == m_activeCallId) {
        m_activeCallId = kInvalidCallId;
        // Promote a held call (if any) to active.
        for (auto &kv : m_heldState) {
            if (!kv.second) continue;
            CallImpl *target = nullptr;
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                auto callIt = m_calls.find(kv.first);
                if (callIt != m_calls.end()) target = callIt->second.get();
            }
            if (!target || !isConfirmedState(kv.first)) continue;
            m_activeCallId = kv.first;
            pj::CallOpParam prm;
            prm.opt.flag = PJSUA_CALL_UNHOLD;
            prm.opt.audioCount = 1;
            prm.opt.videoCount = 0;
            try { target->reinvite(prm); } catch (...) {}
            m_heldState[kv.first] = false;
            break;
        }
    }
}

CallId CallManager::makeCall(const std::string &uri)
{
    if (!m_am) return kInvalidCallId;
    return makeCall(m_am->defaultAccountId(), uri);
}

CallId CallManager::makeCall(AccountId accountId, const std::string &uri)
{
    if (!m_am) {
        spdlog::error("CallManager::makeCall: no AccountsManager");
        return kInvalidCallId;
    }
    auto *pjAcc = m_am->pjAccountFor(accountId);
    if (!pjAcc) {
        spdlog::error("CallManager::makeCall: no registered pj::Account for id {}",
                      accountId);
        return kInvalidCallId;
    }
    CallId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
    auto call = std::make_unique<CallImpl>(*pjAcc, this, id, accountId);
    pj::CallOpParam prm(true);
    try {
        call->makeCall(uri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::makeCall failed: {}", e.info());
        return kInvalidCallId;
    }
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calls.emplace(id, std::move(call));
        m_callAccount[id] = accountId;
    }
    setActiveCall(id);
    return id;
}

CallId CallManager::adoptIncomingCall(AccountId accountId, int pjsipCallId)
{
    // Runs on the PJSIP worker thread (see CallsController: adoption must be
    // synchronous or the INVITE session tears down before we claim it).
    if (!m_am) return kInvalidCallId;
    auto *pjAcc = m_am->pjAccountFor(accountId);
    if (!pjAcc) return kInvalidCallId;

    CallId id = m_nextId.fetch_add(1, std::memory_order_relaxed);
    auto call = std::make_unique<CallImpl>(*pjAcc, this, id, accountId, pjsipCallId);
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_calls.emplace(id, std::move(call));
        m_callAccount[id] = accountId;
    }
    notifyStateChange(id, CallState::Calling);
    return id;
}

void CallManager::setActiveCall(CallId id)
{
    if (m_activeCallId == id) return;
    // Hold the previous active call (if any and not already held). We use
    // setHold directly (not the public hold()) to avoid round-trip through
    // additional bookkeeping.
    const CallId previousId = m_activeCallId;
    CallImpl *previous = nullptr;
    if (previousId != kInvalidCallId) {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto previousIt = m_calls.find(previousId);
        if (previousIt != m_calls.end()) previous = previousIt->second.get();
    }
    if (previous && isConfirmedState(previousId) && !isHeld(previousId)) {
        pj::CallOpParam prm;
        try { previous->setHold(prm); } catch (...) {}
        m_heldState[previousId] = true;
    }
    m_activeCallId = id;
    // Unhold the new active call if it was held.
    if (id != kInvalidCallId && isHeld(id)) {
        CallImpl *next = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_calls.find(id);
            if (it != m_calls.end()) next = it->second.get();
        }
        if (next) {
            pj::CallOpParam prm;
            prm.opt.flag = PJSUA_CALL_UNHOLD;
            prm.opt.audioCount = 1;
            prm.opt.videoCount = 0;
            try { next->reinvite(prm); } catch (...) {}
            m_heldState[id] = false;
        }
    }
}

bool CallManager::accept(CallId id)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    // Promote to active first — auto-holds whichever call was active.
    setActiveCall(id);
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_OK;
    try {
        call->answer(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::accept: {}", e.info());
        return false;
    }
    return true;
}

bool CallManager::decline(CallId id)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_DECLINE;
    try {
        call->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::decline: {}", e.info());
        return false;
    }
    return true;
}

namespace {
// Returns the first ACTIVE AudioMedia* for the given call, or nullptr.
pj::AudioMedia *firstActiveAudio(pj::Call *call)
{
    if (!call) return nullptr;
    auto info = call->getInfo();
    for (unsigned i = 0; i < info.media.size(); ++i) {
        if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
            info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
            return static_cast<pj::AudioMedia *>(call->getMedia(i));
        }
    }
    return nullptr;
}
} // namespace

bool CallManager::mergeCalls(CallId activeCallId, CallId heldCallId)
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_calls.find(activeCallId) == m_calls.end() ||
            m_calls.find(heldCallId) == m_calls.end()) {
            return false;
        }
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
    CallImpl *a = nullptr;
    CallImpl *b = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto aIt = m_calls.find(activeCallId);
        auto bIt = m_calls.find(heldCallId);
        if (aIt == m_calls.end() || bIt == m_calls.end()) return false;
        a = aIt->second.get();
        b = bIt->second.get();
    }

    auto *audA = firstActiveAudio(a);
    auto *audB = firstActiveAudio(b);
    if (!audA || !audB) {
        if (retriesRemaining <= 0) {
            spdlog::warn("mergeCalls: active audio did not become ready");
            return false;
        }
        QTimer::singleShot(400, this,
                           [this, activeCallId, heldCallId, retriesRemaining] {
            wireBridge(activeCallId, heldCallId, retriesRemaining - 1);
        });
        return true;
    }
    try {
        audA->startTransmit(*audB);
        audB->startTransmit(*audA);
    } catch (const pj::Error &e) {
        spdlog::error("mergeCalls: bridge failed: {}", e.info());
        return false;
    }
    spdlog::info("CallManager: merged calls {} and {} into conference",
                 activeCallId, heldCallId);
    return true;
}

bool CallManager::startRecording(CallId id, const std::string &outputPath)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    if (outputPath.empty()) return false;
    auto *aud = firstActiveAudio(call);
    if (!aud) {
        spdlog::warn("CallManager::startRecording: no active audio yet on {}",
                     id);
        return false;
    }
    return m_recorder->start(id, aud, outputPath);
}

bool CallManager::stopRecording(CallId id)
{
    return m_recorder->stop(id);
}

bool CallManager::isRecording(CallId id) const
{
    return m_recorder->isRecording(id);
}

bool CallManager::playAudioFile(CallId id, const std::string &path, bool loop)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    if (path.empty()) return false;

    auto *aud = firstActiveAudio(call);
    if (!aud) {
        spdlog::warn("CallManager::playAudioFile: no active audio yet on {}",
                     id);
        return false;
    }

    try {
        stopAudioFile(id);
        auto player = std::make_unique<pj::AudioMediaPlayer>();
        player->createPlayer(path, loop ? 0 : PJMEDIA_FILE_NO_LOOP);
        player->startTransmit(*aud);
        m_players[id] = std::move(player);
        spdlog::info("CallManager: playing file into call {} <- {}", id, path);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::playAudioFile: {}", e.info());
        return false;
    }
}

bool CallManager::stopAudioFile(CallId id)
{
    auto it = m_players.find(id);
    if (it == m_players.end()) return false;
    it->second.reset();
    m_players.erase(it);
    spdlog::info("CallManager: stopped file playback for call {}", id);
    return true;
}

bool CallManager::isPlayingAudioFile(CallId id) const
{
    return m_players.count(id) > 0;
}

bool CallManager::forwardCall(CallId id, const std::string &targetUri)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    if (targetUri.empty()) return false;
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_MOVED_TEMPORARILY;   // 302
    prm.reason = "Forwarded";
    pj::SipHeader contact;
    contact.hName = "Contact";
    // Wrap bare URIs in angle brackets so name-addr parsing succeeds.
    contact.hValue = (targetUri.front() == '<')
                         ? targetUri
                         : ("<" + targetUri + ">");
    prm.txOption.headers.push_back(contact);
    try {
        call->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::forwardCall: {}", e.info());
        return false;
    }
    spdlog::info("CallManager: forwarded call {} to {}", id, targetUri);
    return true;
}

bool CallManager::blindTransfer(CallId id, const std::string &targetUri)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    m_transfers.record(id, {id});
    pj::CallOpParam prm;
    try {
        call->xfer(targetUri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::blindTransfer: {}", e.info());
        m_transfers.drop(id);
        return false;
    }
    spdlog::info("Blind transfer of call {} to {}", id, targetUri);
    cleanupTransferredCalls(id);
    return true;
}

bool CallManager::attendedTransfer(CallId activeCallId, CallId destCallId)
{
    if (activeCallId == destCallId) return false;
    CallImpl *active = nullptr;
    CallImpl *dest = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto activeIt = m_calls.find(activeCallId);
        auto destIt = m_calls.find(destCallId);
        if (activeIt == m_calls.end() || destIt == m_calls.end()) return false;
        active = activeIt->second.get();
        dest = destIt->second.get();
    }
    m_transfers.record(activeCallId, {activeCallId, destCallId});
    pj::CallOpParam prm;
    try {
        active->xferReplaces(*dest, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::attendedTransfer: {}", e.info());
        m_transfers.drop(activeCallId);
        return false;
    }
    spdlog::info("Attended transfer of {} to {}", activeCallId, destCallId);
    cleanupTransferredCalls(activeCallId);
    return true;
}

void CallManager::cleanupTransferredCalls(CallId transferCallId)
{
    const auto cleanupIds = m_transfers.take(transferCallId);
    if (cleanupIds.empty()) return;

    for (const auto cleanupId : cleanupIds) {
        CallImpl *call = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto callIt = m_calls.find(cleanupId);
            if (callIt != m_calls.end()) call = callIt->second.get();
        }
        if (!call) continue;
        pj::CallOpParam prm;
        prm.statusCode = PJSIP_SC_OK;
        try {
            call->hangup(prm);
        } catch (const pj::Error &e) {
            spdlog::error("CallManager::cleanupTransferredCalls: {}", e.info());
        }
    }
}

void CallManager::handleTransferStatus(CallId id, int statusCode,
                                       bool finalNotify,
                                       const std::string &reason)
{
    // Always runs on the main thread — CallImpl::onCallTransferStatus
    // marshals here via a queued QMetaObject::invokeMethod.
    if (!finalNotify) return;

    const auto cleanupIds = m_transfers.take(id);
    if (cleanupIds.empty()) return;

    if (statusCode < 200 || statusCode >= 300) {
        spdlog::warn("Transfer of call {} finished with {} {}", id,
                     statusCode, reason);
        return;
    }

    for (const auto cleanupId : cleanupIds) {
        CallImpl *call = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto callIt = m_calls.find(cleanupId);
            if (callIt != m_calls.end()) call = callIt->second.get();
        }
        if (!call) continue;
        pj::CallOpParam prm;
        prm.statusCode = PJSIP_SC_OK;
        try {
            call->hangup(prm);
        } catch (const pj::Error &e) {
            spdlog::error("CallManager::handleTransferStatus: {}", e.info());
        }
    }
}

void CallManager::hangup(CallId id)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return;
        call = it->second.get();
    }
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_DECLINE;
    try { call->hangup(prm); }
    catch (const pj::Error &e) {
        spdlog::error("CallManager::hangup error: {}", e.info());
    }
}

bool CallManager::hold(CallId id)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    pj::CallOpParam prm;
    try {
        call->setHold(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::hold: {}", e.info());
        return false;
    }
    m_heldState[id] = true;
    return true;
}

bool CallManager::unhold(CallId id)
{
    return requestUnhold(id, 5);
}

bool CallManager::requestUnhold(CallId id, int retriesRemaining)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }
    pj::CallOpParam prm;
    prm.opt.flag = PJSUA_CALL_UNHOLD;
    prm.opt.audioCount = 1;
    prm.opt.videoCount = 0;
    try {
        call->reinvite(prm);
    } catch (const pj::Error &e) {
        if (retriesRemaining > 0) {
            spdlog::warn("CallManager::unhold deferred: {}", e.info());
            QTimer::singleShot(300, this, [this, id, retriesRemaining]() {
                requestUnhold(id, retriesRemaining - 1);
            });
            m_heldState[id] = false;
            return true;
        }
        spdlog::error("CallManager::unhold: {}", e.info());
        return false;
    }
    m_heldState[id] = false;
    // Promote to active. Inlined (rather than setActiveCall) to avoid the
    // recursive re-invite that setActiveCall would issue for held targets.
    if (m_activeCallId != id) {
        const CallId previousId = m_activeCallId;
        CallImpl *previous = nullptr;
        if (previousId != kInvalidCallId) {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto previousIt = m_calls.find(previousId);
            if (previousIt != m_calls.end()) {
                previous = previousIt->second.get();
            }
        }
        if (previous && isConfirmedState(previousId) && !isHeld(previousId)) {
            pj::CallOpParam holdPrm;
            try { previous->setHold(holdPrm); } catch (...) {}
            m_heldState[previousId] = true;
        }
        m_activeCallId = id;
    }
    return true;
}

bool CallManager::isHeld(CallId id) const
{
    auto it = m_heldState.find(id);
    return it != m_heldState.end() && it->second;
}

bool CallManager::setMuted(CallId id, bool muted)
{
    CallImpl *call = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
    }

    auto info = call->getInfo();
    pj::AudioMedia *aud = nullptr;
    for (unsigned i = 0; i < info.media.size(); ++i) {
        if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
            info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
            aud = static_cast<pj::AudioMedia *>(call->getMedia(i));
            break;
        }
    }
    if (!aud) {
        // No active audio yet — record the desired state and report success;
        // onCallMediaState applies it when the stream (re)activates.
        spdlog::info("CallManager::setMuted: no active audio for call {}; "
                     "deferring to media activation", id);
        std::lock_guard<std::mutex> lock(m_mutex);
        m_mutedState[id] = muted;
        return true;
    }

    // Record the desired state BEFORE touching the conference bridge. PJSIP
    // dispatches onCallMediaState with the PJSUA lock held, and our transmit
    // calls below take that same lock — so with the record published first,
    // a media-activation callback either starts after the record (reads the
    // new state and honours it) or is already in flight (finishes its wiring
    // before our transmit call can acquire the lock, which then applies the
    // new state last). Recording after applying loses exactly that race: a
    // callback between our transmit call and the record reads the stale
    // state and silently re-opens a just-muted microphone (Asterisk sends a
    // re-INVITE right after answer, so this happens in practice).
    bool previous;
    bool hadPrevious;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto stateIt = m_mutedState.find(id);
        hadPrevious = stateIt != m_mutedState.end();
        previous = hadPrevious && stateIt->second;
        m_mutedState[id] = muted;
    }

    auto &mgr = pj::Endpoint::instance().audDevManager();
    try {
        if (muted) {
            mgr.getCaptureDevMedia().stopTransmit(*aud);
        } else {
            mgr.getCaptureDevMedia().startTransmit(*aud);
        }
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::setMuted: {}", e.info());
        std::lock_guard<std::mutex> lock(m_mutex);
        if (hadPrevious) {
            m_mutedState[id] = previous;
        } else {
            m_mutedState.erase(id);
        }
        return false;
    }
    return true;
}

bool CallManager::isMuted(CallId id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_mutedState.find(id);
    return it != m_mutedState.end() && it->second;
}

bool CallManager::sendDtmf(CallId id, const std::string &digits)
{
    CallImpl *call = nullptr;
    AccountId acctId = kInvalidAccountId;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return false;
        call = it->second.get();
        auto acctIt = m_callAccount.find(id);
        if (acctIt == m_callAccount.end()) return false;
        acctId = acctIt->second;
    }
    const auto account = m_am ? m_am->find(acctId) : std::nullopt;
    const auto method = account ? account->dtmfMethod : DtmfMethod::Rfc2833;

    try {
        if (method == DtmfMethod::Info) {
            for (char c : digits) {
                pj::CallSendDtmfParam prm;
                prm.method = PJSUA_DTMF_METHOD_SIP_INFO;
                prm.digits = std::string(1, c);
                prm.duration = PJSUA_CALL_SEND_DTMF_DURATION_DEFAULT;
                call->sendDtmf(prm);
            }
        } else {
            call->dialDtmf(digits);
        }
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::sendDtmf: {}", e.info());
        return false;
    }
    return true;
}

std::vector<CallEntry> CallManager::snapshot() const
{
    // Collect ids, raw call pointers, and account ids under the lock; do the
    // PJSIP getInfo() calls after releasing it (PJSIP takes its own lock —
    // holding ours across that call deadlocks against the worker-thread
    // callbacks). The raw pointers stay valid because the main thread is the
    // only eraser of m_calls.
    struct LiveCall {
        CallId id;
        CallImpl *call;
        AccountId accountId;
    };
    std::vector<LiveCall> live;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        live.reserve(m_calls.size());
        for (const auto &[id, call] : m_calls) {
            auto acctIt = m_callAccount.find(id);
            live.push_back({id, call.get(),
                            acctIt != m_callAccount.end()
                                ? acctIt->second
                                : kInvalidAccountId});
        }
    }

    std::vector<CallEntry> out;
    out.reserve(live.size() + m_lingeringCalls.size());
    for (const auto &[id, call, accountId] : live) {
        CallEntry e;
        e.id = id;
        e.accountId = accountId;
        try {
            auto info = call->getInfo();
            // PJSIP empties remoteUri once the call disconnects, so we cache
            // the last non-empty value and fall back to it during the
            // post-hangup grace period.
            if (!info.remoteUri.empty()) {
                m_remoteUriCache[id] = info.remoteUri;
            }
            if (!info.remoteContact.empty()) {
                m_remoteDisplayCache[id] = info.remoteContact;
            }
            auto cachedUri = m_remoteUriCache.find(id);
            auto cachedDn  = m_remoteDisplayCache.find(id);
            e.remoteUri = !info.remoteUri.empty()
                              ? info.remoteUri
                              : (cachedUri != m_remoteUriCache.end()
                                     ? cachedUri->second : std::string{});
            e.remoteDisplayName = !info.remoteContact.empty()
                              ? info.remoteContact
                              : (cachedDn != m_remoteDisplayCache.end()
                                     ? cachedDn->second : std::string{});
            switch (info.state) {
            case PJSIP_INV_STATE_CALLING:
            case PJSIP_INV_STATE_INCOMING:    e.state = CallState::Calling; break;
            case PJSIP_INV_STATE_EARLY:       e.state = CallState::EarlyMedia; break;
            case PJSIP_INV_STATE_CONFIRMED:   e.state = CallState::Confirmed; break;
            case PJSIP_INV_STATE_DISCONNECTED:e.state = CallState::Disconnected; break;
            default: e.state = CallState::Idle; break;
            }
            e.direction = info.role == PJSIP_ROLE_UAS
                              ? CallDirection::Inbound
                              : CallDirection::Outbound;
        } catch (...) {}
        e.held = isHeld(id);
        e.muted = isMuted(id);
        e.recording = isRecording(id);
        out.push_back(e);
    }
    for (const auto &[id, call] : m_lingeringCalls) {
        CallEntry e;
        e.id = id;
        e.accountId = call.accountId;
        e.remoteUri = call.remoteUri;
        e.remoteDisplayName = call.remoteDisplayName;
        e.state = call.state;
        e.held = call.held;
        e.muted = call.muted;
        e.recording = call.recording;
        e.direction = call.inbound ? CallDirection::Inbound
                                   : CallDirection::Outbound;
        out.push_back(std::move(e));
    }
    return out;
}

void CallManager::setOnCallStateChanged(std::function<void(CallState)> cb)
{
    m_cb = std::move(cb);
}

void CallManager::setOnCallEvent(std::function<void(CallId, CallState)> cb)
{
    m_eventCb = std::move(cb);
}

void CallManager::notifyStateChange(CallId id, CallState s)
{
    // Runs on the PJSIP worker thread (onCallState / adoptIncomingCall).
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_callStates[id] = s;
    }
    // Invoke the callbacks outside the lock — consumers queue work to the
    // main thread and may call back into CallManager.
    if (m_cb) m_cb(s);
    if (m_eventCb) m_eventCb(id, s);
}

bool CallManager::isConfirmedState(CallId id) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    auto it = m_callStates.find(id);
    return it != m_callStates.end() && it->second == CallState::Confirmed;
}

} // namespace compactphone::sip

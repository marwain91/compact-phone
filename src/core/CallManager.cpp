#include "CallManager.h"
#include "AccountsManager.h"
#include "CallEntry.h"

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
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;

            auto *aud = static_cast<pj::AudioMedia *>(getMedia(i));
            if (!aud) continue;

            auto &mgr = pj::Endpoint::instance().audDevManager();
            try {
                aud->startTransmit(mgr.getPlaybackDevMedia());
                mgr.getCaptureDevMedia().startTransmit(*aud);
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
        m_owner->handleTransferStatus(m_localId,
                                      static_cast<int>(prm.statusCode),
                                      prm.finalNotify,
                                      prm.reason);
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
    : QObject(parent), m_am(am) {}
CallManager::~CallManager() = default;

size_t CallManager::callCount() const
{
    return m_calls.size();
}

CallManager::StreamStats CallManager::streamStats(CallId id) const
{
    StreamStats out;
    auto it = m_calls.find(id);
    if (it == m_calls.end() || !it->second) return out;

    try {
        pj::CallInfo info = it->second->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status != PJSUA_CALL_MEDIA_ACTIVE) continue;

            pj::StreamStat s = it->second->getStreamStat(i);
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
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return;

    LingeringCallSnapshot lingering;
    auto acctIt = m_callAccount.find(id);
    lingering.accountId = acctIt != m_callAccount.end()
                              ? acctIt->second
                              : kInvalidAccountId;
    lingering.held = isHeld(id);
    lingering.muted = isMuted(id);
    lingering.recording = isRecording(id);
    try {
        const auto info = it->second->getInfo();
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

    m_calls.erase(it);
    m_callAccount.erase(id);
    m_heldState.erase(id);
    m_mutedState.erase(id);
    m_transferCleanup.erase(id);
    m_remoteUriCache.erase(id);
    m_remoteDisplayCache.erase(id);
    m_recorders.erase(id);
    m_lingeringCalls[id] = std::move(lingering);

    if (id == m_activeCallId) {
        m_activeCallId = kInvalidCallId;
        for (auto &kv : m_heldState) {
            auto callIt = m_calls.find(kv.first);
            if (kv.second && callIt != m_calls.end() &&
                isConfirmedState(kv.first)) {
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
    const bool present = m_calls.count(id) > 0 ||
                         m_lingeringCalls.count(id) > 0;
    m_calls.erase(id);
    m_lingeringCalls.erase(id);
    m_callAccount.erase(id);
    m_callStates.erase(id);
    m_heldState.erase(id);
    m_mutedState.erase(id);
    m_transferCleanup.erase(id);
    m_remoteUriCache.erase(id);
    m_remoteDisplayCache.erase(id);
    m_recorders.erase(id);   // closes the WAV file if still recording
    if (present) emit callsChanged();

    if (id == m_activeCallId) {
        m_activeCallId = kInvalidCallId;
        // Promote a held call (if any) to active.
        for (auto &kv : m_heldState) {
            auto callIt = m_calls.find(kv.first);
            if (kv.second && callIt != m_calls.end() &&
                isConfirmedState(kv.first)) {
                m_activeCallId = kv.first;
                pj::CallOpParam prm;
                prm.opt.flag = PJSUA_CALL_UNHOLD;
                prm.opt.audioCount = 1;
                prm.opt.videoCount = 0;
                try { callIt->second->reinvite(prm); } catch (...) {}
                m_heldState[kv.first] = false;
                break;
            }
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
    CallId id = m_nextId++;
    auto call = std::make_unique<CallImpl>(*pjAcc, this, id, accountId);
    pj::CallOpParam prm(true);
    try {
        call->makeCall(uri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::makeCall failed: {}", e.info());
        return kInvalidCallId;
    }
    m_calls.emplace(id, std::move(call));
    m_callAccount[id] = accountId;
    setActiveCall(id);
    return id;
}

CallId CallManager::adoptIncomingCall(AccountId accountId, int pjsipCallId)
{
    if (!m_am) return kInvalidCallId;
    auto *pjAcc = m_am->pjAccountFor(accountId);
    if (!pjAcc) return kInvalidCallId;

    CallId id = m_nextId++;
    auto call = std::make_unique<CallImpl>(*pjAcc, this, id, accountId, pjsipCallId);
    m_calls.emplace(id, std::move(call));
    m_callAccount[id] = accountId;
    notifyStateChange(id, CallState::Calling);
    return id;
}

void CallManager::setActiveCall(CallId id)
{
    if (m_activeCallId == id) return;
    // Hold the previous active call (if any and not already held). We use
    // setHold directly (not the public hold()) to avoid round-trip through
    // additional bookkeeping.
    auto previousIt = m_calls.find(m_activeCallId);
    if (m_activeCallId != kInvalidCallId && previousIt != m_calls.end()
        && isConfirmedState(m_activeCallId)
        && !isHeld(m_activeCallId)) {
        pj::CallOpParam prm;
        try { previousIt->second->setHold(prm); } catch (...) {}
        m_heldState[m_activeCallId] = true;
    }
    m_activeCallId = id;
    // Unhold the new active call if it was held.
    if (id != kInvalidCallId && m_calls.count(id) && isHeld(id)) {
        pj::CallOpParam prm;
        prm.opt.flag = PJSUA_CALL_UNHOLD;
        prm.opt.audioCount = 1;
        prm.opt.videoCount = 0;
        try { m_calls[id]->reinvite(prm); } catch (...) {}
        m_heldState[id] = false;
    }
}

bool CallManager::accept(CallId id)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    // Promote to active first — auto-holds whichever call was active.
    setActiveCall(id);
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_OK;
    try {
        it->second->answer(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::accept: {}", e.info());
        return false;
    }
    return true;
}

bool CallManager::decline(CallId id)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_DECLINE;
    try {
        it->second->hangup(prm);
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
    auto a = m_calls.find(activeCallId);
    auto b = m_calls.find(heldCallId);
    if (a == m_calls.end() || b == m_calls.end()) return false;
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
    auto a = m_calls.find(activeCallId);
    auto b = m_calls.find(heldCallId);
    if (a == m_calls.end() || b == m_calls.end()) return false;

    auto *audA = firstActiveAudio(a->second.get());
    auto *audB = firstActiveAudio(b->second.get());
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
    auto it = m_calls.find(id);
    if (it == m_calls.end() || outputPath.empty()) return false;
    if (m_recorders.count(id)) return true;   // already recording

    auto *aud = firstActiveAudio(it->second.get());
    if (!aud) {
        spdlog::warn("CallManager::startRecording: no active audio yet on {}",
                     id);
        return false;
    }
    try {
        auto rec = std::make_unique<pj::AudioMediaRecorder>();
        rec->createRecorder(outputPath);
        // Mix both directions into the recorder.
        auto &mgr = pj::Endpoint::instance().audDevManager();
        mgr.getCaptureDevMedia().startTransmit(*rec);
        aud->startTransmit(*rec);
        m_recorders[id] = std::move(rec);
        spdlog::info("CallManager: recording call {} -> {}", id, outputPath);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::startRecording: {}", e.info());
        return false;
    }
}

bool CallManager::stopRecording(CallId id)
{
    auto it = m_recorders.find(id);
    if (it == m_recorders.end()) return false;
    // Destroying the recorder flushes and closes the WAV file.
    it->second.reset();
    m_recorders.erase(it);
    spdlog::info("CallManager: stopped recording call {}", id);
    return true;
}

bool CallManager::isRecording(CallId id) const
{
    return m_recorders.count(id) > 0;
}

bool CallManager::forwardCall(CallId id, const std::string &targetUri)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end() || targetUri.empty()) return false;
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
        it->second->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::forwardCall: {}", e.info());
        return false;
    }
    spdlog::info("CallManager: forwarded call {} to {}", id, targetUri);
    return true;
}

bool CallManager::blindTransfer(CallId id, const std::string &targetUri)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    m_transferCleanup[id] = {id};
    pj::CallOpParam prm;
    try {
        it->second->xfer(targetUri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::blindTransfer: {}", e.info());
        m_transferCleanup.erase(id);
        return false;
    }
    spdlog::info("Blind transfer of call {} to {}", id, targetUri);
    cleanupTransferredCalls(id);
    return true;
}

bool CallManager::attendedTransfer(CallId activeCallId, CallId destCallId)
{
    auto activeIt = m_calls.find(activeCallId);
    auto destIt = m_calls.find(destCallId);
    if (activeIt == m_calls.end() || destIt == m_calls.end()) return false;
    if (activeCallId == destCallId) return false;
    m_transferCleanup[activeCallId] = {activeCallId, destCallId};
    pj::CallOpParam prm;
    try {
        activeIt->second->xferReplaces(*destIt->second, prm);
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::attendedTransfer: {}", e.info());
        m_transferCleanup.erase(activeCallId);
        return false;
    }
    spdlog::info("Attended transfer of {} to {}", activeCallId, destCallId);
    cleanupTransferredCalls(activeCallId);
    return true;
}

void CallManager::cleanupTransferredCalls(CallId transferCallId)
{
    auto cleanupIt = m_transferCleanup.find(transferCallId);
    if (cleanupIt == m_transferCleanup.end()) return;

    auto cleanupIds = std::move(cleanupIt->second);
    m_transferCleanup.erase(cleanupIt);

    for (const auto cleanupId : cleanupIds) {
        auto callIt = m_calls.find(cleanupId);
        if (callIt == m_calls.end()) continue;
        pj::CallOpParam prm;
        prm.statusCode = PJSIP_SC_OK;
        try {
            callIt->second->hangup(prm);
        } catch (const pj::Error &e) {
            spdlog::error("CallManager::cleanupTransferredCalls: {}", e.info());
        }
    }
}

void CallManager::handleTransferStatus(CallId id, int statusCode,
                                       bool finalNotify,
                                       const std::string &reason)
{
    if (!finalNotify) return;

    auto cleanupIt = m_transferCleanup.find(id);
    if (cleanupIt == m_transferCleanup.end()) return;

    auto cleanupIds = std::move(cleanupIt->second);
    m_transferCleanup.erase(cleanupIt);

    if (statusCode < 200 || statusCode >= 300) {
        spdlog::warn("Transfer of call {} finished with {} {}", id,
                     statusCode, reason);
        return;
    }

    for (const auto cleanupId : cleanupIds) {
        auto callIt = m_calls.find(cleanupId);
        if (callIt == m_calls.end()) continue;
        pj::CallOpParam prm;
        prm.statusCode = PJSIP_SC_OK;
        try {
            callIt->second->hangup(prm);
        } catch (const pj::Error &e) {
            spdlog::error("CallManager::handleTransferStatus: {}", e.info());
        }
    }
}

void CallManager::hangup(CallId id)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return;
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_DECLINE;
    try { it->second->hangup(prm); }
    catch (const pj::Error &e) {
        spdlog::error("CallManager::hangup error: {}", e.info());
    }
}

bool CallManager::hold(CallId id)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    pj::CallOpParam prm;
    try {
        it->second->setHold(prm);
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
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    pj::CallOpParam prm;
    prm.opt.flag = PJSUA_CALL_UNHOLD;
    prm.opt.audioCount = 1;
    prm.opt.videoCount = 0;
    try {
        it->second->reinvite(prm);
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
        auto previousIt = m_calls.find(m_activeCallId);
        if (m_activeCallId != kInvalidCallId && previousIt != m_calls.end()
            && isConfirmedState(m_activeCallId)
            && !isHeld(m_activeCallId)) {
            pj::CallOpParam holdPrm;
            try { previousIt->second->setHold(holdPrm); } catch (...) {}
            m_heldState[m_activeCallId] = true;
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
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;

    auto info = it->second->getInfo();
    pj::AudioMedia *aud = nullptr;
    for (unsigned i = 0; i < info.media.size(); ++i) {
        if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
            info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
            aud = static_cast<pj::AudioMedia *>(it->second->getMedia(i));
            break;
        }
    }
    if (!aud) {
        spdlog::warn("CallManager::setMuted: no active audio for call {}", id);
        m_mutedState[id] = muted;
        return false;
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
        return false;
    }
    m_mutedState[id] = muted;
    return true;
}

bool CallManager::isMuted(CallId id) const
{
    auto it = m_mutedState.find(id);
    return it != m_mutedState.end() && it->second;
}

bool CallManager::sendDtmf(CallId id, const std::string &digits)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end()) return false;
    auto acctIt = m_callAccount.find(id);
    if (acctIt == m_callAccount.end()) return false;
    const auto account = m_am ? m_am->find(acctIt->second) : std::nullopt;
    const auto method = account ? account->dtmfMethod : DtmfMethod::Rfc2833;

    try {
        if (method == DtmfMethod::Info) {
            for (char c : digits) {
                pj::CallSendDtmfParam prm;
                prm.method = PJSUA_DTMF_METHOD_SIP_INFO;
                prm.digits = std::string(1, c);
                prm.duration = PJSUA_CALL_SEND_DTMF_DURATION_DEFAULT;
                it->second->sendDtmf(prm);
            }
        } else {
            it->second->dialDtmf(digits);
        }
    } catch (const pj::Error &e) {
        spdlog::error("CallManager::sendDtmf: {}", e.info());
        return false;
    }
    return true;
}

std::vector<CallEntry> CallManager::snapshot() const
{
    std::vector<CallEntry> out;
    out.reserve(m_calls.size() + m_lingeringCalls.size());
    for (const auto &[id, call] : m_calls) {
        CallEntry e;
        e.id = id;
        auto acctIt = m_callAccount.find(id);
        e.accountId = acctIt != m_callAccount.end()
                          ? acctIt->second
                          : kInvalidAccountId;
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
    m_callStates[id] = s;
    if (m_cb) m_cb(s);
    if (m_eventCb) m_eventCb(id, s);
}

bool CallManager::isConfirmedState(CallId id) const
{
    auto it = m_callStates.find(id);
    return it != m_callStates.end() && it->second == CallState::Confirmed;
}

} // namespace compactphone::sip

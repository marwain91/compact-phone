#include "PjsipBackend.h"
#include "MwiSummary.h"
#include "RemoteInfo.h"

#include "core/SipEngine.h"

#include <pjsua-lib/pjsua.h>
#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

#include <type_traits>

namespace compactphone::sipbackend {

// Guard: every ISipBackend pure virtual must be overridden — the class
// must be concrete. A static_assert catches a missed override before any
// test tries to instantiate this class.
static_assert(!std::is_abstract_v<PjsipBackend>,
              "PjsipBackend is still abstract — a pure virtual is missing");

// ---------------------------------------------------------------------------
// Anonymous helpers — moved from AccountsManager's anonymous namespace in
// phase 2. AccountsManager's copy was removed in the same phase.
// ---------------------------------------------------------------------------

namespace {

const char *transportScheme(sipbackend::Transport t)
{
    switch (t) {
    case sipbackend::Transport::Tcp: return ";transport=tcp";
    case sipbackend::Transport::Tls: return ";transport=tls";
    case sipbackend::Transport::Udp:
    default:
        // Explicitly state UDP so PJSIP does not size-escalate to TCP for
        // large requests (RFC 3261 §18.1.1) and so any cached non-UDP
        // connection to the registrar isn't reused. The parameter also
        // makes the chosen transport visible on the wire / in pcaps.
        return ";transport=udp";
    }
}

} // namespace

// ---------------------------------------------------------------------------
// PjsipAccount — pj::Account subclass
//
// Lives entirely in this TU so the header stays pj-clean.
//
// Threading: all pj::Account callbacks (onRegState, onIncomingCall, etc.)
// run on the PJSUA worker thread. They may ONLY:
//   (a) Call owner->post* helpers, which marshal via m_events.post() —
//       thread-safe by EventDispatch's contract.
//   (b) Touch owner->m_nativeCallIds under owner->m_nativeMutex
//       (announceIncomingCall, held briefly, PJSIP not called while held).
// They must NOT call any other PJSIP API or touch any shared state without
// the appropriate lock.
// ---------------------------------------------------------------------------

class PjsipBackend::PjsipAccount : public pj::Account {
public:
    AccountId id = kInvalidAccountId;
    // Raw pointer is safe: PjsipBackend owns this object; pj::Account's
    // destructor serializes against in-flight callback dispatch via the
    // PJSUA lock — by the time PjsipBackend dies, no callback can still
    // be running.
    PjsipBackend *owner = nullptr;

    void onRegState(pj::OnRegStateParam &prm) override
    {
        pj::AccountInfo info;
        try {
            info = getInfo();
        } catch (const pj::Error &e) {
            // getInfo() only fails once pjsua has invalidated the account
            // (shutdown/removal racing a final reg event). Nothing to
            // report — and the exception must not unwind into PJSIP's C
            // frames.
            spdlog::warn("PjsipAccount {} onRegState getInfo failed: {}", id,
                         e.info());
            return;
        }
        spdlog::info("PjsipAccount {} reg state: active={} code={} reason='{}'",
                     id, info.regIsActive, static_cast<int>(prm.code),
                     prm.reason);
        // Post the raw triple. Policy (mapRegEvent) stays manager-side.
        owner->postRegState(id, info.regIsActive,
                            static_cast<int>(prm.code), prm.reason);
    }

    void onIncomingCall(pj::OnIncomingCallParam &prm) override
    {
        // Delegate to wrapIncomingCall which constructs a PjsipCall eagerly
        // (inside the callback, retaining the INVITE session) and posts to
        // the listener. The hook short-circuit inside wrapIncomingCall
        // ensures the phase-2 adoption path is used when a hook is installed.
        owner->wrapIncomingCall(*this, id, prm.callId);
    }

    // Inbound SIP MESSAGE (RFC 3428 instant message).
    void onInstantMessage(pj::OnInstantMessageParam &prm) override
    {
        owner->postInstantMessage(id, prm.fromUri, prm.msgBody);
    }

    // Server sent a SIMPLE NOTIFY for message-summary (voicemail).
    // PJSIP hands us the raw NOTIFY body in prm.rdata.wholeMsg.
    void onMwiInfo(pj::OnMwiInfoParam &prm) override
    {
        const auto s = parseMwiSummary(prm.rdata.wholeMsg);
        spdlog::info("PjsipAccount {} MWI: new={} old={} active={}",
                     id, s.newMessages, s.oldMessages, s.active);
        owner->postMwi(id, s.newMessages, s.oldMessages, s.active);
    }
};

// ---------------------------------------------------------------------------
// PjsipCall — pj::Call subclass (the relocated CallManager::CallImpl)
//
// Threading: all pj::Call callbacks run on the PJSUA worker thread (and,
// for makeCall/hangup, can be dispatched SYNCHRONOUSLY on the main thread
// inside the initiating call — pjsua re-enters onCallState before
// makeCall() returns). Callbacks therefore only:
//   (a) call owner->post* helpers (thread-safe EventDispatch::post), and
//   (b) touch this object's own m_muted atomic.
// They must NOT touch owner->m_calls — the entry for this call may not be
// inserted yet when a synchronous callback fires (makeCall inserts after
// the pj call returns), and map access would need m_callsMutex, which the
// initiating main-thread command may already hold logic around.
// ---------------------------------------------------------------------------

class PjsipBackend::PjsipCall : public pj::Call {
public:
    PjsipCall(pj::Account &acc, PjsipBackend *owner, CallId id,
              int sysCallId = PJSUA_INVALID_ID)
        : pj::Call(acc, sysCallId), m_owner(owner), m_id(id) {}

    // Desired mute state. Written by the main thread (setMuted), read by
    // the PJSUA thread (onCallMediaState honouring mute across re-INVITE
    // renegotiation). Atomic — this is the relocated m_mutedState cross-
    // thread pair, shrunk from a mutex-guarded map to one flag per call.
    std::atomic<bool> m_muted{false};

    void onCallState(pj::OnCallStateParam & /*prm*/) override
    {
        // getInfo() only fails once pjsua has invalidated the call —
        // teardown raced this callback. Fall through as DISCONNECTED:
        // letting the pj::Error unwind through PJSIP's C frames aborts the
        // process, and swallowing the event would leak this PjsipCall in
        // m_calls forever (no later state callback will ever come).
        auto state = PJSIP_INV_STATE_DISCONNECTED;
        std::string stateText = "(call already gone)";
        int lastCode = 0;
        try {
            const auto info = getInfo();
            state = info.state;
            stateText = info.stateText;
            lastCode = static_cast<int>(info.lastStatusCode);
        } catch (const pj::Error &e) {
            spdlog::warn("PjsipCall {} onCallState getInfo failed: {}", m_id,
                         e.info());
        }
        spdlog::info("PjsipCall {} state {} -> {}", m_id,
                     static_cast<int>(state), stateText);
        switch (state) {
        case PJSIP_INV_STATE_CALLING:
        case PJSIP_INV_STATE_INCOMING:
            m_owner->postCallState(m_id, CallState::Calling, lastCode); break;
        case PJSIP_INV_STATE_EARLY:
            m_owner->postCallState(m_id, CallState::EarlyMedia, lastCode); break;
        case PJSIP_INV_STATE_CONFIRMED:
            m_owner->postCallState(m_id, CallState::Confirmed, lastCode); break;
        case PJSIP_INV_STATE_DISCONNECTED:
            // lastCode rides the event itself — the manager can read the
            // final disposition the moment it sees Disconnected (this
            // replaces recordDisconnectCode + the lastStatusCode map read).
            m_owner->postCallState(m_id, CallState::Disconnected, lastCode); break;
        default: break;
        }
        // NOTE: the releaseCallToGrace invokeMethod that CallImpl had here is
        // GONE — release is now manager-driven through releaseCall().
    }

    void onCallMediaState(pj::OnCallMediaStateParam & /*prm*/) override
    {
        pj::CallInfo info;
        try {
            info = getInfo();
        } catch (const pj::Error &e) {
            // Call torn down between the media event and now — nothing to
            // wire, and the exception must not unwind into PJSIP's C frames.
            spdlog::warn("PjsipCall {} onCallMediaState getInfo failed: {}",
                         m_id, e.info());
            return;
        }
        // Media (re)activates not just on call setup but on every re-INVITE —
        // hold/unhold, peer-initiated renegotiation, codec changes. Honour
        // the recorded mute state when wiring capture, or a re-INVITE would
        // silently re-open the microphone on a muted call (and a mute pressed
        // before media became active would never be applied).
        const bool muted = m_muted.load(std::memory_order_acquire);
        bool anyActive = false;
        bool anyHeld = false;
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                anyActive = true;
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
                    spdlog::error("PjsipCall audio wiring error: {}", e.info());
                }
            } else if (info.media[i].status == PJSUA_CALL_MEDIA_LOCAL_HOLD) {
                anyHeld = true;
            }
        }
        // Contract rule 5: PJSIP does not dispatch onCallMediaState for the
        // disconnect transition itself, so the adapter naturally never emits
        // onMediaState for a call that just ended — Disconnected implies
        // media-inactive.
        m_owner->postMediaState(m_id, anyActive, anyHeld);
    }

    void onCallTransferStatus(pj::OnCallTransferStatusParam &prm) override
    {
        spdlog::info("PjsipCall {} transfer status {} {} final={}", m_id,
                     static_cast<int>(prm.statusCode), prm.reason,
                     prm.finalNotify);
        const int statusCode = static_cast<int>(prm.statusCode);
        const bool finalNotify = prm.finalNotify;
        const std::string reason = prm.reason;
        // Stop the REFER subscription after a final 2xx (protocol, not policy).
        if (finalNotify && prm.statusCode >= 200 && prm.statusCode < 300)
            prm.cont = false;
        m_owner->postTransferStatus(m_id, statusCode, finalNotify, reason);
    }

    CallId id() const { return m_id; }

private:
    // Raw pointer is safe: PjsipBackend owns this object and destroys every
    // PjsipCall (and PjsipAccount) before m_events / itself dies — see the
    // EventDispatch quiesce contract and ~PjsipBackend.
    PjsipBackend *m_owner;
    CallId m_id;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PjsipBackend::PjsipBackend(sip::SipEngine *engine)
    : m_engine(engine)
{
}

PjsipBackend::~PjsipBackend()
{
    // Same teardown as stop() but without stopping the borrowed engine:
    // every PjsipCall/PjsipAccount must be gone (quiescing the PJSUA
    // posting threads) before m_events is destroyed.
    m_recorders.clear();
    m_filePlayers.clear();
    std::map<CallId, std::unique_ptr<PjsipCall>> doomed;
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        doomed.swap(m_calls);
    }
    doomed.clear();
    m_graceCalls.clear();
    m_accounts.clear();
    // Straggler sweep: an onIncomingCall in flight during the swap above
    // may have inserted a fresh wrapper before the accounts died.
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        doomed.swap(m_calls);
    }
    doomed.clear();
}

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PjsipBackend::setListener(ISipBackendListener *listener)
{
    // Quiesce barrier: invalidate first so lambdas queued for the previous
    // listener no-op when they run (contract rule 4: no event delivered after
    // setListener(nullptr)), then assign the new listener.
    m_events.invalidate();
    m_listener = listener;
}

bool PjsipBackend::start(const EngineConfig &cfg)
{
    // NOTE (phase-2 dual-ownership): PhoneController may have already started
    // SipEngine directly. SipEngine::start() no-ops returning true when already
    // running, so cfg.sipPort is NOT applied to the live endpoint in that case.
    return m_engine->start(cfg.sipPort);
}

void PjsipBackend::stop()
{
    m_recorders.clear();
    m_filePlayers.clear();
    // Swap live calls out under the lock; destroy outside it (destructors
    // call into PJSIP and must not hold m_callsMutex — see the lock
    // discipline note in the header).
    std::map<CallId, std::unique_ptr<PjsipCall>> doomed;
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        doomed.swap(m_calls);
    }
    doomed.clear();
    m_graceCalls.clear();
    // Accounts next: pj::Account destructors serialize against in-flight
    // callback dispatch via the PJSUA lock — after clear() returns, no
    // PJSIP thread is inside wrapIncomingCall / any post helper.
    m_accounts.clear();
    // Straggler sweep: an onIncomingCall in flight during the swap above
    // may have inserted a fresh wrapper before the accounts died.
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        doomed.swap(m_calls);
    }
    doomed.clear();
    // Presence watch bookkeeping: cleared on stop so a restarted backend
    // starts empty (contract rule: "stop() drops all accounts, calls, and
    // watches"). No real SIP un-SUBSCRIBE here — phase 5 handles teardown
    // of actual pj::Buddy subscriptions.
    m_watches.clear();
    // Invalidate the event queue so nothing queued before this fires.
    m_events.invalidate();
    m_engine->stop();
}

bool PjsipBackend::isRunning() const
{
    return m_engine->isRunning();
}

// ---------------------------------------------------------------------------
// Engine-level config — thin delegation to SipEngine
// ---------------------------------------------------------------------------

void PjsipBackend::setCaTrust(const CaTrust &trust)
{
    // Store the CA file path. In-memory PEM resolution (Windows ROOT store)
    // happens inside SipEngine::start() — it reads m_caCertBuf there, and
    // that logic does NOT move here. We therefore only forward the file path.
    // trust.caPem is deliberately not forwarded: SipEngine resolves the
    // Windows ROOT store internally and owns the in-memory PEM path.
    //
    // Ordering note: this value only affects transports created afterwards.
    // Call setCaTrust() before start() so the shared TLS transport picks it
    // up; per-account transports read the setting at registration time.
    m_engine->setCaCertFile(trust.caFile);
}

void PjsipBackend::setStunServers(const std::vector<std::string> &servers)
{
    m_engine->applyStunServers(servers);
}

void PjsipBackend::setCodecPriority(const std::vector<std::string> &priorityOrder)
{
    m_engine->applyCodecPriority(priorityOrder);
}

std::vector<AudioDevice> PjsipBackend::audioDevices() const
{
    std::vector<AudioDevice> result;
    for (const auto &dev : m_engine->audioDevices()) {
        result.push_back({dev.id, dev.name, dev.inputCount, dev.outputCount});
    }
    return result;
}

int PjsipBackend::captureDevice() const
{
    return m_engine->captureDevice();
}

int PjsipBackend::playbackDevice() const
{
    return m_engine->playbackDevice();
}

bool PjsipBackend::setCaptureDevice(int id)
{
    return m_engine->setCaptureDevice(id);
}

bool PjsipBackend::setPlaybackDevice(int id)
{
    return m_engine->setPlaybackDevice(id);
}

void PjsipBackend::refreshAudioDevices()
{
    m_engine->refreshAudioDevices();
}

// ---------------------------------------------------------------------------
// Phase 5 stubs — ringtone and log sink
// ---------------------------------------------------------------------------

bool PjsipBackend::playRingtone(const std::string & /*path*/)
{
    // phase 5 stub: RingtonePlayer integration not yet wired through the adapter
    return false;
}

void PjsipBackend::stopRingtone()
{
    // phase 5 stub
}

void PjsipBackend::setLogSink(std::function<void(int, const std::string &)> /*sink*/)
{
    // phase 5 stub: SipLog wiring through the adapter is deferred
}

// ---------------------------------------------------------------------------
// Private post helpers — called from PjsipAccount callbacks (PJSUA thread)
// ---------------------------------------------------------------------------

void PjsipBackend::postRegState(AccountId id, bool regIsActive,
                                int sipCode, const std::string &reason)
{
    m_events.post([this, id, regIsActive, sipCode, reason] {
        if (m_listener)
            m_listener->onRegState(id, regIsActive, sipCode, reason);
    });
}

void PjsipBackend::postInstantMessage(AccountId id,
                                      const std::string &fromUri,
                                      const std::string &body)
{
    m_events.post([this, id, fromUri, body] {
        if (m_listener)
            m_listener->onInstantMessage(id, fromUri, body);
    });
}

void PjsipBackend::postMwi(AccountId id, int newMessages,
                           int oldMessages, bool active)
{
    m_events.post([this, id, newMessages, oldMessages, active] {
        if (m_listener)
            m_listener->onMwi(id, newMessages, oldMessages, active);
    });
}

// ---------------------------------------------------------------------------
// Call post helpers — called from PjsipCall callbacks (PJSUA thread)
// ---------------------------------------------------------------------------

void PjsipBackend::postCallState(CallId id, CallState s, int sipCode)
{
    m_events.post([this, id, s, sipCode] {
        if (s == CallState::Disconnected) {
            // Adapter-private prompt teardown of per-call media objects:
            // destroying the recorder flushes + closes the WAV; destroying
            // the player stops transmission. Main thread, no lock held —
            // both destructors call into PJSIP.
            m_recorders.erase(id);
            m_filePlayers.erase(id);
        }
        if (m_listener)
            m_listener->onCallState(id, s, sipCode);
    });
}

void PjsipBackend::postMediaState(CallId id, bool active, bool held)
{
    m_events.post([this, id, active, held] {
        if (m_listener)
            m_listener->onMediaState(id, active, held);
    });
}

void PjsipBackend::postTransferStatus(CallId id, int sipCode, bool isFinal,
                                      const std::string &reason)
{
    m_events.post([this, id, sipCode, isFinal, reason] {
        if (m_listener)
            m_listener->onTransferStatus(id, sipCode, isFinal, reason);
    });
}

// ---------------------------------------------------------------------------
// wrapIncomingCall — called from PjsipAccount::onIncomingCall (PJSUA thread)
// ---------------------------------------------------------------------------

void PjsipBackend::wrapIncomingCall(PjsipAccount &account, AccountId accId,
                                    int pjsipCallId)
{
    // PJSUA WORKER THREAD.
    //
    // Transitional (deleted in the CallManager-rewire commit): while the
    // phase-2 hook is installed, the old synchronous-adoption path runs and
    // the eager wrap below is skipped — two pj::Call wrappers for one
    // pjsua call id would corrupt pjsua2's call map.
    {
        std::lock_guard<std::mutex> lk(m_hookMutex);
        if (m_nativeIncomingHook) {
            m_nativeIncomingHook(accId, pjsipCallId);
            return;
        }
    }

    // Claim the INVITE session NOW, inside the callback (see the
    // declaration comment). Construction registers the wrapper in pjsua2's
    // call map; subsequent callbacks for this call land on the PjsipCall.
    const CallId id = m_nextCallId.fetch_add(1, std::memory_order_relaxed);
    auto call = std::make_unique<PjsipCall>(account, this, id, pjsipCallId);

    // Push-model remote identity, captured at event time (PJSIP clears
    // remoteUri after disconnect — capturing here is what lets the URI
    // caches in CallManager die).
    std::string remoteUri;
    std::string displayName;
    try {
        const auto info = call->getInfo();
        const auto ri = parseRemoteInfo(info.remoteUri);  // name-addr form
        remoteUri = ri.uri;
        displayName = ri.displayName;
    } catch (const pj::Error &e) {
        spdlog::warn("PjsipBackend::wrapIncomingCall: getInfo failed: {}",
                     e.info());
        // Announce anyway with empty identity rather than dropping the call.
    }

    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        m_calls.emplace(id, std::move(call));
    }
    spdlog::info("PjsipBackend: incoming call account={} callId={} pjsip={} "
                 "uri='{}' display='{}'",
                 accId, id, pjsipCallId, remoteUri, displayName);

    m_events.post([this, accId, id, remoteUri, displayName] {
        if (m_listener)
            m_listener->onIncomingCall(accId, id, remoteUri, displayName);
    });
}

// ---------------------------------------------------------------------------
// announceIncomingCall — superseded by wrapIncomingCall (deleted in Task 4).
// Kept for one commit so this TU still compiles; no longer called.
// ---------------------------------------------------------------------------

void PjsipBackend::announceIncomingCall(AccountId accId, int pjsipCallId)
{
    // Mint a backend CallId atomically (no mutex needed for the counter itself).
    const CallId callId = m_nextCallId.fetch_add(1, std::memory_order_relaxed);

    // Store the native-id mapping under m_nativeMutex. Lock scope is
    // deliberately minimal — PJSIP is never called while the lock is held.
    {
        std::lock_guard<std::mutex> lk(m_nativeMutex);
        m_nativeCallIds[callId] = pjsipCallId;
    }

    // If a synchronous hook is registered, fire it under m_hookMutex and
    // return immediately. The hook runs ON THE PJSIP THREAD — the caller
    // (CallsController/CallManager) adopts the call synchronously here,
    // which avoids the PJSIP_ESESSIONTERMINATED race that would occur if
    // adoption were deferred to the main thread. The queued listener
    // onIncomingCall is suppressed: the hook owner takes responsibility for
    // the full announcement (it posts its own main-thread notification).
    {
        std::lock_guard<std::mutex> lk(m_hookMutex);
        if (m_nativeIncomingHook) {
            m_nativeIncomingHook(accId, pjsipCallId);
            return;
        }
    }

    // No hook — resolve remote display info via the PJSUA C API and post
    // to the listener. This is safe to call on the PJSUA worker thread
    // (the call is in ringing state by now). If it fails we still announce
    // with empty strings rather than dropping the announcement.
    std::string remoteUri;
    std::string displayName;

    pjsua_call_info ci;
    if (pjsua_call_get_info(static_cast<pjsua_call_id>(pjsipCallId), &ci)
            == PJ_SUCCESS) {
        // ci.remote_info has the raw SIP Address-of-Record string.
        const std::string raw(ci.remote_info.ptr,
                              static_cast<std::size_t>(ci.remote_info.slen));
        const auto ri = parseRemoteInfo(raw);
        remoteUri    = ri.uri;
        displayName  = ri.displayName;
    } else {
        spdlog::warn("PjsipBackend::announceIncomingCall: "
                     "pjsua_call_get_info failed for pjsip call id {}",
                     pjsipCallId);
    }

    spdlog::info("PjsipBackend: incoming call account={} callId={} "
                 "pjsip={} uri='{}' display='{}'",
                 accId, callId, pjsipCallId, remoteUri, displayName);

    m_events.post([this, accId, callId, remoteUri, displayName] {
        if (m_listener)
            m_listener->onIncomingCall(accId, callId, remoteUri, displayName);
    });
}

// ---------------------------------------------------------------------------
// Accounts
// ---------------------------------------------------------------------------

AccountId PjsipBackend::addAccount(const AccountSettings &settings)
{
    if (!m_engine || !m_engine->isRunning()) {
        spdlog::error("PjsipBackend::addAccount: engine not running");
        return kInvalidAccountId;
    }

    // Moved from AccountsManager::registerAccount in phase 2.
    pj::AccountConfig acfg;
    const bool tls = settings.transport == Transport::Tls;
    const std::string scheme = tls ? "sips:" : "sip:";
    acfg.idUri = settings.hideCallerId
        ? scheme + "anonymous@anonymous.invalid"
        : scheme + settings.username + "@" + settings.domain;
    acfg.regConfig.registrarUri =
        scheme + settings.domain
        + (tls ? "" : transportScheme(settings.transport));
    acfg.regConfig.timeoutSec = settings.registerIntervalSec > 0
        ? settings.registerIntervalSec : 300;

    // Auth credentials: default authUser to username, realm to "*" when empty.
    const auto authUser = settings.authUser.empty()
                              ? settings.username
                              : settings.authUser;
    const auto authRealm = settings.authRealm.empty()
                               ? std::string{"*"}
                               : settings.authRealm;
    pj::AuthCredInfo cred("digest", authRealm, authUser, 0, settings.password);
    acfg.sipConfig.authCreds.push_back(cred);

    // Outbound proxy
    if (!settings.proxy.empty()) {
        std::string proxy = settings.proxy;
        if (proxy.rfind("sip:", 0) != 0 && proxy.rfind("sips:", 0) != 0) {
            proxy = scheme + proxy + transportScheme(settings.transport);
        }
        acfg.sipConfig.proxies.push_back(proxy);
    }

    // NAT helpers
    if (!settings.publicAddress.empty()) {
        acfg.sipConfig.contactForced = scheme + settings.username + "@"
                                       + settings.publicAddress;
    }

    // STUN opt-in: when AccountsManager sets useStun (from !stunServer.empty()),
    // ask PJSIP to use the global STUN config for this account.
    // The STUN server itself must be set at endpoint init time (SipEngine::start).
    // Per-account dynamic STUN isn't exposed by PJSUA2 — that's a v1 enhancement.
    if (settings.useStun) {
        acfg.natConfig.sipStunUse = PJSUA_STUN_USE_DEFAULT;
        acfg.natConfig.mediaStunUse = PJSUA_STUN_USE_DEFAULT;
    }

    if (settings.iceEnabled) {
        acfg.natConfig.iceEnabled = true;
    }
    acfg.regConfig.registerOnAdd = true;
    if (settings.keepaliveIntervalSec > 0) {
        acfg.natConfig.udpKaIntervalSec = settings.keepaliveIntervalSec;
    }
    if (!settings.sessionTimersEnabled) {
        acfg.callConfig.timerUse = PJSUA_SIP_TIMER_INACTIVE;
    }
    acfg.presConfig.publishEnabled = settings.publishPresenceEnabled;

    // Subscribe to message-summary so the server can push voicemail
    // notifications. PJSIP issues SUBSCRIBE shortly after REGISTER.
    acfg.mwiConfig.enabled = true;

    // SRTP per spec §3.1
    switch (settings.srtpMode) {
    case SrtpMode::Disabled:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_DISABLED;
        break;
    case SrtpMode::Optional:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_OPTIONAL;
        break;
    case SrtpMode::Required:
        acfg.mediaConfig.srtpUse = PJMEDIA_SRTP_MANDATORY;
        break;
    }
    acfg.mediaConfig.srtpSecureSignaling = 0;

    // Per-account TLS verify: create a dedicated TLS transport with this
    // account's verify policy and bind the account to it via transportId.
    // PJSUA2 doesn't expose verifyServer on AccountSipConfig, so a per-
    // account transport is the supported way to control TLS verify.
    if (tls) {
        try {
            pj::TransportConfig tlsCfg;
            tlsCfg.port = 0;
            tlsCfg.tlsConfig.method = PJSIP_TLSV1_2_METHOD;
            tlsCfg.tlsConfig.verifyServer = !settings.allowUntrustedCert;
            tlsCfg.tlsConfig.verifyClient = false;
            // Trust anchors so a verifying account can actually accept a
            // legitimately-signed cert. Shared with the engine's resolved CA
            // trust (PEM file on Linux/macOS, OS ROOT store buffer on Windows).
            if (m_engine) m_engine->applyCaTrust(tlsCfg.tlsConfig);
            const auto tpId = pj::Endpoint::instance()
                .transportCreate(PJSIP_TRANSPORT_TLS, tlsCfg);
            acfg.sipConfig.transportId = tpId;
        } catch (const pj::Error &err) {
            spdlog::error("PjsipBackend::addAccount: per-account TLS transport: {}",
                          err.info());
            return kInvalidAccountId;
        }
    }

    const AccountId newId = m_nextAccountId++;
    auto impl = std::make_unique<PjsipAccount>();
    impl->id = newId;
    impl->owner = this;
    try {
        impl->create(acfg);
    } catch (const pj::Error &err) {
        spdlog::error("PjsipBackend::addAccount: pj create failed: {}", err.info());
        return kInvalidAccountId;
    }
    m_accounts[newId] = std::move(impl);
    spdlog::info("PjsipBackend::addAccount: registered backend account id={}", newId);
    return newId;
}

bool PjsipBackend::removeAccount(AccountId id)
{
    // Stale queued onRegState/onMwi events for the removed id are expected
    // after this returns: setRegistration(false) fires a final reg event that
    // may arrive on the main thread after the pj::Account is gone. Listener-
    // side lookups (AccountsManager) must therefore tolerate unknown ids
    // gracefully rather than treating them as errors.
    const auto it = m_accounts.find(id);
    if (it == m_accounts.end()) return false;
    try {
        it->second->setRegistration(false);
    } catch (...) {}
    m_accounts.erase(it);
    return true;
}

bool PjsipBackend::sendMessage(AccountId id, const std::string &toUri,
                               const std::string &body)
{
    const auto it = m_accounts.find(id);
    if (it == m_accounts.end()) return false;
    // Every stored unique_ptr is non-null by construction (addAccount always
    // moves a freshly created unique_ptr into the map).

    // PJSUA2 only exposes sendInstantMessage on Buddy/Call (i.e. inside
    // an existing dialog/subscription). For one-shot out-of-dialog
    // MESSAGE the C API pjsua_im_send is the right tool.
    const int pjAccId = it->second->getId();
    pj_str_t toStr = pj_str(const_cast<char *>(toUri.c_str()));
    pj_str_t mime  = pj_str(const_cast<char *>("text/plain"));
    pj_str_t cont  = pj_str(const_cast<char *>(body.c_str()));
    const pj_status_t st = pjsua_im_send(pjAccId, &toStr, &mime,
                                         &cont, nullptr, nullptr);
    if (st != PJ_SUCCESS) {
        spdlog::error("PjsipBackend::sendMessage: pjsua_im_send={}", st);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Presence — bookkeeping only; SIP SUBSCRIBE/pj::Buddy land in phase 5
// ---------------------------------------------------------------------------

WatchId PjsipBackend::watch(AccountId accountId, const std::string & /*uri*/)
{
    // Phase 5 will initiate a real SIP SUBSCRIBE / pj::Buddy here; for now
    // we satisfy the contract's id-lifetime rules with pure bookkeeping:
    //   - unknown account (not in m_accounts): return kInvalidWatchId
    //   - known account: mint and store a WatchId, return it
    // The uri is intentionally not stored — phase 5 stores it in pj::Buddy.
    if (!m_engine->isRunning() || m_accounts.count(accountId) == 0)
        return kInvalidWatchId;
    const WatchId id = m_nextWatchId++;
    m_watches[id] = accountId;
    spdlog::debug("PjsipBackend::watch: id={} account={} (presence phase-5 stub)",
                  id, accountId);
    return id;
}

bool PjsipBackend::unwatch(WatchId id)
{
    // Phase 5 will cancel the pj::Buddy subscription here. For now just
    // prune the bookkeeping entry; return false if already gone (contract
    // rule: false on double-unwatch).
    if (m_watches.erase(id) == 0)
        return false;
    spdlog::debug("PjsipBackend::unwatch: id={} (presence phase-5 stub)", id);
    return true;
}

// ---------------------------------------------------------------------------
// Call layer — real implementations
// ---------------------------------------------------------------------------

namespace {

// Returns the first ACTIVE AudioMedia* for the given call, or nullptr.
// Swallows pj::Error: getInfo() throws once pjsua has invalidated the call
// (teardown racing the caller), and every caller already handles "no active
// audio" — while an exception would escape into the Qt event loop.
pj::AudioMedia *firstActiveAudio(pj::Call *call)
{
    if (!call) return nullptr;
    try {
        auto info = call->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
                info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                return static_cast<pj::AudioMedia *>(call->getMedia(i));
            }
        }
    } catch (const pj::Error &e) {
        spdlog::warn("firstActiveAudio: getInfo failed: {}", e.info());
    }
    return nullptr;
}

} // namespace

PjsipBackend::PjsipCall *PjsipBackend::liveCall(CallId id) const
{
    std::lock_guard<std::mutex> lk(m_callsMutex);
    const auto it = m_calls.find(id);
    return it != m_calls.end() ? it->second.get() : nullptr;
}

CallId PjsipBackend::makeCall(AccountId accountId, const std::string &uri)
{
    // Account lookup via m_accounts (main-thread map, no lock needed).
    const auto accIt = m_accounts.find(accountId);
    if (accIt == m_accounts.end()) {
        spdlog::error("PjsipBackend::makeCall: no account for id {}", accountId);
        return kInvalidCallId;
    }
    const CallId id = m_nextCallId.fetch_add(1, std::memory_order_relaxed);
    auto call = std::make_unique<PjsipCall>(*accIt->second, this, id);
    pj::CallOpParam prm(true);
    try {
        call->makeCall(uri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::makeCall failed: {}", e.info());
        return kInvalidCallId;
    }
    // Note: pjsua dispatches onCallState(CALLING) synchronously inside
    // call->makeCall() — the PjsipCall only posts, so insertion-after-call
    // is safe and the queued event is delivered after the manager has recorded
    // the id.
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        m_calls.emplace(id, std::move(call));
    }
    return id;
}

bool PjsipBackend::answer(CallId id)
{
    auto *call = liveCall(id);
    if (!call) return false;
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_OK;
    try {
        call->answer(prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::answer: {}", e.info());
        return false;
    }
    return true;
}

bool PjsipBackend::decline(CallId id, int sipCode)
{
    auto *call = liveCall(id);
    if (!call) return false;
    pj::CallOpParam prm;
    prm.statusCode = static_cast<pjsip_status_code>(sipCode);
    try {
        call->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::decline: {}", e.info());
        return false;
    }
    return true;
}

bool PjsipBackend::redirect(CallId id, const std::string &contactUri)
{
    auto *call = liveCall(id);
    if (!call) return false;
    if (contactUri.empty()) return false;
    pj::CallOpParam prm;
    prm.statusCode = PJSIP_SC_MOVED_TEMPORARILY;   // 302
    prm.reason = "Forwarded";
    pj::SipHeader contact;
    contact.hName = "Contact";
    // Wrap bare URIs in angle brackets so name-addr parsing succeeds.
    contact.hValue = (contactUri.front() == '<')
                         ? contactUri
                         : ("<" + contactUri + ">");
    prm.txOption.headers.push_back(contact);
    try {
        call->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::redirect: {}", e.info());
        return false;
    }
    spdlog::info("PjsipBackend: redirected call {} to {}", id, contactUri);
    return true;
}

void PjsipBackend::hangup(CallId id)
{
    auto *call = liveCall(id);
    if (!call) return;
    pj::CallOpParam prm;
    // PJSIP_SC_DECLINE: pre-answer sends CANCEL (correct); for confirmed
    // calls pjsua maps it to a plain BYE — equivalent to SC_OK post-answer.
    prm.statusCode = PJSIP_SC_DECLINE;
    try {
        call->hangup(prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::hangup error: {}", e.info());
    }
}

bool PjsipBackend::hold(CallId id)
{
    auto *call = liveCall(id);
    if (!call) return false;
    pj::CallOpParam prm;
    try {
        call->setHold(prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::hold: {}", e.info());
        return false;
    }
    return true;
}

bool PjsipBackend::unhold(CallId id)
{
    auto *call = liveCall(id);
    if (!call) return false;
    pj::CallOpParam prm;
    prm.opt.flag = PJSUA_CALL_UNHOLD;
    prm.opt.audioCount = 1;
    prm.opt.videoCount = 0;
    try {
        call->reinvite(prm);
    } catch (const pj::Error &e) {
        spdlog::warn("PjsipBackend::unhold: {}", e.info());
        return false;  // manager retries
    }
    return true;
}

bool PjsipBackend::setMuted(CallId id, bool muted)
{
    auto *call = liveCall(id);
    if (!call) return false;

    pj::AudioMedia *aud = nullptr;
    try {
        auto info = call->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type == PJMEDIA_TYPE_AUDIO &&
                info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) {
                aud = static_cast<pj::AudioMedia *>(call->getMedia(i));
                break;
            }
        }
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::setMuted: getInfo failed: {}", e.info());
        return false;
    }
    if (!aud) {
        // No active audio yet — record the desired state and report success;
        // onCallMediaState applies it when the stream (re)activates.
        spdlog::info("PjsipBackend::setMuted: no active audio for call {}; "
                     "deferring to media activation", id);
        call->m_muted.store(muted, std::memory_order_release);
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
    const bool previous = call->m_muted.exchange(muted, std::memory_order_acq_rel);

    auto &mgr = pj::Endpoint::instance().audDevManager();
    try {
        if (muted) {
            mgr.getCaptureDevMedia().stopTransmit(*aud);
        } else {
            mgr.getCaptureDevMedia().startTransmit(*aud);
        }
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::setMuted: {}", e.info());
        // Rollback: restore the previous mute state.
        call->m_muted.store(previous, std::memory_order_release);
        return false;
    }
    return true;
}

bool PjsipBackend::sendDtmf(CallId id, const std::string &digits,
                             DtmfMethod method)
{
    auto *call = liveCall(id);
    if (!call) return false;
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
        spdlog::error("PjsipBackend::sendDtmf: {}", e.info());
        return false;
    }
    return true;
}

bool PjsipBackend::blindTransfer(CallId id, const std::string &targetUri)
{
    auto *call = liveCall(id);
    if (!call) return false;
    pj::CallOpParam prm;
    try {
        call->xfer(targetUri, prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::blindTransfer: {}", e.info());
        return false;
    }
    spdlog::info("PjsipBackend: blind transfer of call {} to {}", id, targetUri);
    return true;
}

bool PjsipBackend::attendedTransfer(CallId id, CallId otherId)
{
    if (id == otherId) return false;
    PjsipCall *active = nullptr;
    PjsipCall *dest = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        auto aIt = m_calls.find(id);
        auto dIt = m_calls.find(otherId);
        if (aIt == m_calls.end() || dIt == m_calls.end()) return false;
        active = aIt->second.get();
        dest = dIt->second.get();
    }
    // Both lookups under ONE m_callsMutex acquisition; release, then
    // call into PJSIP (never hold m_callsMutex while calling PJSIP).
    pj::CallOpParam prm;
    try {
        active->xferReplaces(*dest, prm);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::attendedTransfer: {}", e.info());
        return false;
    }
    spdlog::info("PjsipBackend: attended transfer of {} to {}", id, otherId);
    return true;
}

bool PjsipBackend::bridge(CallId id, CallId otherId)
{
    PjsipCall *a = nullptr;
    PjsipCall *b = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        auto aIt = m_calls.find(id);
        auto bIt = m_calls.find(otherId);
        if (aIt == m_calls.end() || bIt == m_calls.end()) return false;
        a = aIt->second.get();
        b = bIt->second.get();
    }
    auto *audA = firstActiveAudio(a);
    auto *audB = firstActiveAudio(b);
    if (!audA || !audB) return false;  // manager retries
    try {
        audA->startTransmit(*audB);
        audB->startTransmit(*audA);
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::bridge: {}", e.info());
        return false;
    }
    spdlog::info("PjsipBackend: bridged calls {} and {}", id, otherId);
    return true;
}

bool PjsipBackend::startRecording(CallId id, const std::string &path)
{
    auto *call = liveCall(id);
    if (!call) return false;
    if (path.empty()) return false;
    if (m_recorders.count(id)) return true;  // already recording
    auto *aud = firstActiveAudio(call);
    if (!aud) {
        spdlog::warn("PjsipBackend::startRecording: no active audio yet on {}", id);
        return false;
    }
    try {
        auto rec = std::make_unique<pj::AudioMediaRecorder>();
        rec->createRecorder(path);
        // Mix both directions into the recorder.
        auto &mgr = pj::Endpoint::instance().audDevManager();
        mgr.getCaptureDevMedia().startTransmit(*rec);
        aud->startTransmit(*rec);
        m_recorders[id] = std::move(rec);
        spdlog::info("PjsipBackend: recording call {} -> {}", id, path);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::startRecording: {}", e.info());
        return false;
    }
}

bool PjsipBackend::stopRecording(CallId id)
{
    auto it = m_recorders.find(id);
    if (it == m_recorders.end()) return false;
    // Destroying the recorder flushes and closes the WAV file.
    m_recorders.erase(it);
    spdlog::info("PjsipBackend: stopped recording call {}", id);
    return true;
}

bool PjsipBackend::playFile(CallId id, const std::string &path, bool loop)
{
    auto *call = liveCall(id);
    if (!call) return false;
    if (path.empty()) return false;
    auto *aud = firstActiveAudio(call);
    if (!aud) {
        spdlog::warn("PjsipBackend::playFile: no active audio yet on {}", id);
        return false;
    }
    try {
        stopFile(id);  // stop any previous player first
        auto player = std::make_unique<pj::AudioMediaPlayer>();
        player->createPlayer(path, loop ? 0 : PJMEDIA_FILE_NO_LOOP);
        player->startTransmit(*aud);
        m_filePlayers[id] = std::move(player);
        spdlog::info("PjsipBackend: playing file into call {} <- {}", id, path);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("PjsipBackend::playFile: {}", e.info());
        return false;
    }
}

bool PjsipBackend::stopFile(CallId id)
{
    auto it = m_filePlayers.find(id);
    if (it == m_filePlayers.end()) return false;
    it->second.reset();
    m_filePlayers.erase(it);
    spdlog::info("PjsipBackend: stopped file playback for call {}", id);
    return true;
}

StreamStats PjsipBackend::streamStats(CallId id) const
{
    StreamStats out;
    auto *call = liveCall(id);
    if (!call) return out;
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

bool PjsipBackend::isMediaActive(CallId id) const
{
    auto *call = liveCall(id);
    if (!call) return false;
    try {
        const auto info = call->getInfo();
        for (unsigned i = 0; i < info.media.size(); ++i) {
            if (info.media[i].type != PJMEDIA_TYPE_AUDIO) continue;
            if (info.media[i].status == PJSUA_CALL_MEDIA_ACTIVE) return true;
        }
    } catch (const pj::Error &) {
        // Call gone mid-query — no active media.
    }
    return false;
}

bool PjsipBackend::isCaptureTransmitting(CallId id) const
{
    auto *call = liveCall(id);
    if (!call) return false;
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

void PjsipBackend::releaseCall(CallId id)
{
    // Per-call media objects first (closes the WAV if still recording).
    // Main thread, no lock held.
    m_recorders.erase(id);
    m_filePlayers.erase(id);

    std::unique_ptr<PjsipCall> dying;
    {
        std::lock_guard<std::mutex> lk(m_callsMutex);
        auto it = m_calls.find(id);
        if (it == m_calls.end()) return;   // double release: no-op
        dying = std::move(it->second);
        m_calls.erase(it);
    }

    // Do NOT destroy the PjsipCall here. The manager typically calls
    // releaseCall from inside the queued onCallState(Disconnected)
    // delivery, and the PJSIP thread may still be executing the tail of
    // that very callback — pj::Call's destructor does not synchronize with
    // the derived-object reads in that tail (use-after-free, caught live
    // by TSan when this lived in CallManager). Park it; destroy after the
    // grace window, long after the callback has returned. Destroying a
    // still-live call here also hangs it up (pj::Call dtor behavior), so
    // releaseCall on a non-disconnected id is safe, just deferred.
    m_graceCalls[id] = std::move(dying);
    m_events.postDelayed(kGraceDestroyMs, [this, id] {
        m_graceCalls.erase(id);   // pj::Call dtor — main thread, no lock
    });
}

// ---------------------------------------------------------------------------
// Transitional bridges — removed in phase 3
// ---------------------------------------------------------------------------

void PjsipBackend::setNativeIncomingCallHook(std::function<void(AccountId, int)> hook)
{
    std::lock_guard<std::mutex> lk(m_hookMutex);
    m_nativeIncomingHook = std::move(hook);
}

pj::Account *PjsipBackend::pjAccountFor(AccountId id)
{
    const auto it = m_accounts.find(id);
    if (it == m_accounts.end()) return nullptr;
    return it->second.get();  // PjsipAccount IS-A pj::Account
}

int PjsipBackend::nativeCallIdFor(CallId id) const
{
    std::lock_guard<std::mutex> lk(m_nativeMutex);
    const auto it = m_nativeCallIds.find(id);
    return it != m_nativeCallIds.end() ? it->second : -1;
}

} // namespace compactphone::sipbackend

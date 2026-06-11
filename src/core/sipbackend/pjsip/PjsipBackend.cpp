#include "PjsipBackend.h"
#include "MwiSummary.h"

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
// Anonymous helpers (formerly in AccountsManager's anonymous namespace)
// Duplicated here for one commit while AccountsManager still has its own copy.
// Task 5 will remove AccountsManager's copy.
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
        // Delegate to announceIncomingCall which mints the backend CallId,
        // bridges the native id, and posts to the listener.
        owner->announceIncomingCall(id, prm.callId);
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
// Construction / destruction
// ---------------------------------------------------------------------------

PjsipBackend::PjsipBackend(sip::SipEngine *engine)
    : m_engine(engine)
{
}

PjsipBackend::~PjsipBackend() = default;

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
    // Drop all accounts first: pj::Account destructors serialize against
    // in-flight callbacks via the PJSUA lock, so by the time clear() returns
    // no callback thread is still running — safe to destroy m_events next.
    m_accounts.clear();
    // Clear the native-call-id bridge map so a restarted backend starts empty
    // (contract rule). The id counters stay monotonic by design — they are
    // never reset so stale numeric ids from before the stop cannot alias new ones.
    {
        std::lock_guard<std::mutex> lk(m_nativeMutex);
        m_nativeCallIds.clear();
    }
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
// announceIncomingCall — called from PjsipAccount::onIncomingCall (PJSUA thread)
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

    // Resolve remote display info via the PJSUA C API. This is safe to call
    // on the PJSUA worker thread (the call is in ringing state by now).
    // If it fails, we still announce the call with empty strings rather than
    // dropping the announcement.
    std::string remoteUri;
    std::string displayName;

    pjsua_call_info ci;
    if (pjsua_call_get_info(static_cast<pjsua_call_id>(pjsipCallId), &ci)
            == PJ_SUCCESS) {
        // ci.remote_info has the raw SIP Address-of-Record string, which may
        // be one of:
        //   "Display Name" <sip:user@host>
        //   <sip:user@host>
        //   sip:user@host
        const std::string raw(ci.remote_info.ptr,
                              static_cast<std::size_t>(ci.remote_info.slen));
        const auto lt = raw.find('<');
        const auto gt = raw.find('>');
        if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
            remoteUri = raw.substr(lt + 1, gt - lt - 1);
            // Display name is the text before '<'; strip surrounding whitespace
            // and enclosing double-quotes.
            std::string dn = raw.substr(0, lt);
            // Trim trailing whitespace
            while (!dn.empty() && (dn.back() == ' ' || dn.back() == '\t'))
                dn.pop_back();
            // Strip enclosing quotes
            if (dn.size() >= 2 && dn.front() == '"' && dn.back() == '"')
                dn = dn.substr(1, dn.size() - 2);
            displayName = dn;
        } else {
            // No angle brackets — the whole string is the URI
            remoteUri = raw;
        }
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

    // Task 5: STUN opt-in moves to AccountSettings.
    // AccountSettings has no stunServer field (Types.h is not modified in
    // this task). STUN use for per-account scope is therefore deferred.
    // For now, leave sipStunUse/mediaStunUse at their PJSUA defaults
    // (PJSUA_STUN_USE_DEFAULT when a STUN server is configured engine-wide).
    // The field will be added to AccountSettings in Task 5 when
    // AccountsManager builds AccountSettings from Account.

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
    if (it == m_accounts.end() || !it->second) return false;

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
// Presence — phase 5 stubs
// ---------------------------------------------------------------------------

WatchId PjsipBackend::watch(AccountId /*accountId*/, const std::string & /*uri*/)
{
    // phase 5 stub
    return kInvalidWatchId;
}

bool PjsipBackend::unwatch(WatchId /*id*/)
{
    // phase 5 stub
    return false;
}

// ---------------------------------------------------------------------------
// Calls — phase 3 stubs
// All call methods return failure/no-op until the calls path lands.
// ---------------------------------------------------------------------------

CallId PjsipBackend::makeCall(AccountId /*accountId*/, const std::string & /*uri*/)
{
    // phase 3 stub
    return kInvalidCallId;
}

bool PjsipBackend::answer(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::decline(CallId /*id*/, int /*sipCode*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::redirect(CallId /*id*/, const std::string & /*contactUri*/)
{
    // phase 3 stub
    return false;
}

void PjsipBackend::hangup(CallId /*id*/)
{
    // phase 3 stub
}

bool PjsipBackend::hold(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::unhold(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::setMuted(CallId /*id*/, bool /*muted*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::sendDtmf(CallId /*id*/, const std::string & /*digits*/,
                             DtmfMethod /*method*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::blindTransfer(CallId /*id*/, const std::string & /*targetUri*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::attendedTransfer(CallId /*id*/, CallId /*otherId*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::bridge(CallId /*id*/, CallId /*otherId*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::startRecording(CallId /*id*/, const std::string & /*path*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::stopRecording(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::playFile(CallId /*id*/, const std::string & /*path*/, bool /*loop*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::stopFile(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

StreamStats PjsipBackend::streamStats(CallId /*id*/) const
{
    // phase 3 stub: returns an unpopulated stats struct (-1 = not available)
    return {};
}

bool PjsipBackend::isMediaActive(CallId /*id*/) const
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::isCaptureTransmitting(CallId /*id*/) const
{
    // phase 3 stub
    return false;
}

void PjsipBackend::releaseCall(CallId /*id*/)
{
    // phase 3 stub
}

// ---------------------------------------------------------------------------
// Transitional bridges — removed in phase 3
// ---------------------------------------------------------------------------

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

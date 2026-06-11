#pragma once

// PJSIP/PJSUA2 implementation of ISipBackend. Phase 2 scope: accounts,
// registration, MWI, instant messages, incoming-call announcement.
// Engine-level methods delegate to the borrowed SipEngine (engine
// ownership migrates in a later phase); call/presence/ringtone methods
// are explicit phase-3+ stubs that return failure.
//
// pj:: types must not appear in this header's public method signatures
// EXCEPT the two transitional bridges at the bottom, which exist so
// CallManager can stay PJSIP-native until phase 3 — see their comments.

#include "../ISipBackend.h"
#include "../EventDispatch.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

// Forward-declare pj::Account to keep pj headers out of the public API.
namespace pj { class Account; }

namespace compactphone::sip { class SipEngine; }

namespace compactphone::sipbackend {

class PjsipBackend : public ISipBackend {
public:
    // engine outlives the backend; it may be started before or after
    // construction, but accounts can only be added while it runs.
    explicit PjsipBackend(sip::SipEngine *engine);
    ~PjsipBackend() override;

    // --- ISipBackend (lifecycle + engine-level: delegate to SipEngine) ---
    void setListener(ISipBackendListener *listener) override;
    bool start(const EngineConfig &cfg) override;
    // stop(): clears all accounts, invalidates the event queue, then
    // stops the engine. A restarted backend starts empty (contract rule).
    void stop() override;
    bool isRunning() const override;
    // setCaTrust: stores caFile via engine->setCaCertFile().
    // In-memory PEM resolution (Windows ROOT store) happens inside
    // SipEngine::start() and is not repeated here — the engine owns it.
    void setCaTrust(const CaTrust &trust) override;
    void setStunServers(const std::vector<std::string> &servers) override;
    void setCodecPriority(const std::vector<std::string> &priorityOrder) override;
    std::vector<AudioDevice> audioDevices() const override;
    int captureDevice() const override;
    int playbackDevice() const override;
    bool setCaptureDevice(int id) override;
    bool setPlaybackDevice(int id) override;
    void refreshAudioDevices() override;
    // phase 5 stub: ringtone playback is not yet implemented in the adapter
    bool playRingtone(const std::string &path) override;   // phase 5 stub: returns false
    void stopRingtone() override;                          // phase 5 stub: no-op
    void setLogSink(std::function<void(int, const std::string &)> sink) override; // phase 5 stub: no-op

    // --- accounts ---
    AccountId addAccount(const AccountSettings &settings) override;
    bool removeAccount(AccountId id) override;
    bool sendMessage(AccountId id, const std::string &toUri,
                     const std::string &body) override;

    // --- presence — bookkeeping only; SIP SUBSCRIBE/pj::Buddy land in phase 5 ---
    WatchId watch(AccountId accountId, const std::string &uri) override;
    bool unwatch(WatchId id) override;

    // --- calls (phase 3 stubs) ---
    CallId makeCall(AccountId accountId, const std::string &uri) override;
    bool answer(CallId id) override;
    bool decline(CallId id, int sipCode) override;
    bool redirect(CallId id, const std::string &contactUri) override;
    void hangup(CallId id) override;
    bool hold(CallId id) override;
    bool unhold(CallId id) override;
    bool setMuted(CallId id, bool muted) override;
    bool sendDtmf(CallId id, const std::string &digits,
                  DtmfMethod method) override;
    bool blindTransfer(CallId id, const std::string &targetUri) override;
    bool attendedTransfer(CallId id, CallId otherId) override;
    bool bridge(CallId id, CallId otherId) override;
    bool startRecording(CallId id, const std::string &path) override;
    bool stopRecording(CallId id) override;
    bool playFile(CallId id, const std::string &path, bool loop) override;
    bool stopFile(CallId id) override;
    StreamStats streamStats(CallId id) const override;
    bool isMediaActive(CallId id) const override;
    bool isCaptureTransmitting(CallId id) const override;
    void releaseCall(CallId id) override;

    // --- transitional bridges (NOT on ISipBackend; removed in phase 3) ---
    // pjAccountFor: AccountsManager forwards this so CallManager can
    // construct pj::Call against the right pj::Account.
    //
    // nativeCallIdFor: currently UNCONSUMED — incoming calls deliver the
    // native PJSUA call id directly through the synchronous hook, not via
    // this accessor. The map and accessor are retained for phase-3 call
    // adoption (makeCall/answer paths) and removed alongside pjAccountFor.
    pj::Account *pjAccountFor(AccountId id);
    int nativeCallIdFor(CallId id) const;

    // Transitional synchronous incoming-call hook — phase-3-removed alongside
    // pjAccountFor/nativeCallIdFor. The hook fires ON THE PJSIP THREAD with
    // (backendAccountId, pjsipCallId). Invocation happens under m_hookMutex,
    // so setNativeIncomingCallHook({}) is a quiesce barrier: once it returns,
    // no in-flight hook invocation can still be running and no new one will
    // start. Mirror of AccountsManager's m_callbackMutex contract. Never call
    // setNativeIncomingCallHook from inside the hook itself.
    void setNativeIncomingCallHook(std::function<void(AccountId, int)> hook);

private:
    class PjsipAccount;   // pj::Account subclass; defined in .cpp

    // Called from PjsipAccount callbacks (PJSUA worker thread).
    // Each helper marshals one event onto the Qt main thread via m_events.
    // The listener null-check lives inside the posted lambda (same pattern
    // as FakeSipBackend::post) — the listener may be cleared between the
    // post() and the delivery.
    void postRegState(AccountId id, bool regIsActive, int sipCode,
                      const std::string &reason);
    void postInstantMessage(AccountId id, const std::string &fromUri,
                            const std::string &body);
    void postMwi(AccountId id, int newMessages, int oldMessages, bool active);

    // Called from PjsipAccount::onIncomingCall (PJSUA worker thread).
    // Mints a backend CallId, stores the native PJSUA call id under
    // m_nativeMutex, resolves remote info via the C API, then posts
    // onIncomingCall to the listener.
    //
    // Lock discipline: m_nativeMutex is held ONLY while reading/writing
    // m_nativeCallIds and m_nextCallId. NEVER call into PJSIP while
    // holding m_nativeMutex (PJSIP callbacks can re-enter on the same
    // thread and would deadlock).
    void announceIncomingCall(AccountId accId, int pjsipCallId);

    sip::SipEngine *m_engine;
    ISipBackendListener *m_listener = nullptr;
    // Backend id spaces are offset well above zero so that id-confusion bugs
    // (e.g. confusing a backend AccountId with a domain DB row id) fail loudly
    // as out-of-range lookups rather than silently hitting the wrong record.
    AccountId m_nextAccountId = 10001;  // offset: domain DB row ids start at 1
    // m_nextCallId is std::atomic so announceIncomingCall (PJSUA worker
    // thread) and any future main-thread makeCall can mint ids without
    // taking m_nativeMutex (which only guards the map itself).
    std::atomic<CallId> m_nextCallId{50001};  // offset: distinct from account id space
    std::map<AccountId, std::unique_ptr<PjsipAccount>> m_accounts;

    // Presence watch bookkeeping — main-thread only. Phase 5 will replace
    // the map value with a real pj::Buddy subscription object; until then
    // we store only the owning AccountId so watch/unwatch id-lifetime rules
    // (contract: valid id returned for known account, false on double-unwatch,
    // cleared on stop()) are satisfied without initiating any SIP SUBSCRIBE.
    WatchId m_nextWatchId = 80001;  // offset: distinct from account/call spaces
    std::map<WatchId, AccountId> m_watches;  // watchId → owning backendAccountId

    // Guards m_nativeCallIds. PJSUA worker thread writes (announceIncomingCall),
    // main thread reads (nativeCallIdFor). Never held while calling into PJSIP.
    mutable std::mutex m_nativeMutex;
    // Entries are pruned only by stop() clearing the map in phase 2.
    // Phase 3's releaseCall takes over per-call pruning when it lands.
    std::map<CallId, int> m_nativeCallIds;   // incoming announcements, phase-2 only

    // Guards m_nativeIncomingHook. Declared BEFORE m_events so that the hook
    // quiesce barrier (setNativeIncomingCallHook({})) is always usable even
    // after m_events has been invalidated.
    mutable std::mutex m_hookMutex;
    std::function<void(AccountId, int)> m_nativeIncomingHook;

    // Declared LAST: EventDispatch's internal QObject is the invokeMethod
    // context — destroying it cancels undelivered lambdas — so it must die
    // before the maps and other state the lambdas may capture.
    // PjsipAccount destructors (via m_accounts.clear()) must serialize
    // against the pjsua lock before m_events dies — see EventDispatch.h's
    // quiesce contract.
    EventDispatch m_events;
};

} // namespace compactphone::sipbackend

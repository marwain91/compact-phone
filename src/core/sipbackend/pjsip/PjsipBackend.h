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

    // --- presence (phase 5 stubs) ---
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
    // CallManager still constructs pj::Call against the pj::Account and
    // adopts incoming calls by native PJSUA call id. AccountsManager
    // forwards these for it. Both die when the calls path lands.
    pj::Account *pjAccountFor(AccountId id);
    int nativeCallIdFor(CallId id) const;

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
    AccountId m_nextAccountId = 1;
    // m_nextCallId is std::atomic so announceIncomingCall (PJSUA worker
    // thread) and any future main-thread makeCall can mint ids without
    // taking m_nativeMutex (which only guards the map itself).
    std::atomic<CallId> m_nextCallId{1};
    std::map<AccountId, std::unique_ptr<PjsipAccount>> m_accounts;

    // Guards m_nativeCallIds. PJSUA worker thread writes (announceIncomingCall),
    // main thread reads (nativeCallIdFor). Never held while calling into PJSIP.
    mutable std::mutex m_nativeMutex;
    std::map<CallId, int> m_nativeCallIds;   // incoming announcements, phase-2 only

    // Declared LAST: EventDispatch's internal QObject is the invokeMethod
    // context — destroying it cancels undelivered lambdas — so it must die
    // before the maps and other state the lambdas may capture.
    // PjsipAccount destructors (via m_accounts.clear()) must serialize
    // against the pjsua lock before m_events dies — see EventDispatch.h's
    // quiesce contract.
    EventDispatch m_events;
};

} // namespace compactphone::sipbackend

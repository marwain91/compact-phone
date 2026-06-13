#pragma once

// PJSIP/PJSUA2 implementation of ISipBackend. Scope: accounts,
// registration, MWI, instant messages, and the full call layer
// (PjsipCall, hold/unhold re-INVITEs, mute bridge wiring, recording,
// file playback, transfers, the grace-destruction dance, ringtone).
// Presence and log-sink remain phase-5 stubs.
//
// Engine-level methods delegate to the borrowed SipEngine (engine
// ownership migrates in a later phase).
//
// pj:: types must not appear in this header's public method signatures.

#include "../ISipBackend.h"
#include "../EventDispatch.h"

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

// Forward-declare pj:: types to keep pj headers out of the public API.
namespace pj { class Account; class AudioMediaRecorder; class AudioMediaPlayer; }

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
    // Loops a WAV to the playback device (absorbs sip::RingtonePlayer).
    bool playRingtone(const std::string &path) override;
    void stopRingtone() override;
    void setLogSink(std::function<void(int, const std::string &)> sink) override; // phase 5 stub: no-op

    // --- accounts ---
    AccountId addAccount(const AccountSettings &settings) override;
    bool removeAccount(AccountId id) override;
    bool sendMessage(AccountId id, const std::string &toUri,
                     const std::string &body) override;

    // --- presence — one pj::Buddy SUBSCRIBE per watch ---
    WatchId watch(AccountId accountId, const std::string &uri) override;
    bool unwatch(WatchId id) override;

    // --- calls ---
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

private:
    class PjsipAccount;   // pj::Account subclass; defined in .cpp
    class PjsipCall;      // pj::Call subclass; defined in .cpp
    class PjsipBuddy;     // pj::Buddy subclass (presence); defined in .cpp

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
    // Called from PjsipBuddy::onBuddyState (PJSUA worker thread).
    void postPresence(WatchId id, PresenceState state);

    // Called from PjsipCall callbacks (PJSUA worker thread). postCallState's
    // posted lambda additionally drops the call's recorder/file player on
    // Disconnected (main-thread, before notifying the listener) so the WAV
    // closes promptly even if the manager defers releaseCall.
    void postCallState(CallId id, CallState s, int sipCode);
    void postMediaState(CallId id, bool active, bool held);
    void postTransferStatus(CallId id, int sipCode, bool isFinal,
                            const std::string &reason);

    // Called from PjsipAccount::onIncomingCall (PJSUA worker thread).
    // Constructs the PjsipCall EAGERLY — claiming the INVITE session before
    // the callback returns (it tears down within ~15 ms unclaimed, after
    // which answer/decline fail with PJSIP_ESESSIONTERMINATED; this is what
    // the phase-2 synchronous hook existed for) — then resolves remote info
    // and posts onIncomingCall with the data pushed.
    void wrapIncomingCall(PjsipAccount &account, AccountId accId,
                          int pjsipCallId);

    // Main-thread lookup helper: returns the raw PjsipCall* for id (or
    // nullptr), taking and releasing m_callsMutex internally.
    PjsipCall *liveCall(CallId id) const;

    // Un-SUBSCRIBE and destroy every presence buddy, then clear the map. Used
    // by stop()/dtor; the per-watch (unwatch) and per-account (removeAccount)
    // paths do their own un-SUBSCRIBE.
    void clearWatches();

    static constexpr int kGraceDestroyMs = 2200;

    sip::SipEngine *m_engine;
    ISipBackendListener *m_listener = nullptr;
    // Backend id spaces are offset well above zero so that id-confusion bugs
    // (e.g. confusing a backend AccountId with a domain DB row id) fail loudly
    // as out-of-range lookups rather than silently hitting the wrong record.
    AccountId m_nextAccountId = 10001;  // offset: domain DB row ids start at 1
    // m_nextCallId is std::atomic so wrapIncomingCall (PJSUA worker thread)
    // and makeCall (main thread) can mint ids without taking m_callsMutex.
    std::atomic<CallId> m_nextCallId{50001};  // offset: distinct from account id space
    std::map<AccountId, std::unique_ptr<PjsipAccount>> m_accounts;

    // Presence watches — main-thread only (watch/unwatch are main-thread
    // commands). Each entry owns a pj::Buddy whose onBuddyState fires on the
    // PJSUA worker thread and posts onPresence via m_events. The owning
    // accountId lets removeAccount tear down its buddies; buddies are destroyed
    // before m_accounts in stop()/dtor (a buddy outliving its pj::Account would
    // dangle). A null buddy means the SUBSCRIBE setup failed but the WatchId
    // stays live (the id-lifetime contract is satisfied regardless).
    //
    // No grace window is needed (unlike m_calls): pjsua_buddy_del holds the
    // PJSUA lock, so ~pj::Buddy serializes against any in-flight onBuddyState
    // — the same guarantee pj::Account/pj::Call destructors rely on. Buddies
    // can therefore be destroyed synchronously here.
    WatchId m_nextWatchId = 80001;  // offset: distinct from account/call spaces
    struct Watch {
        std::unique_ptr<PjsipBuddy> buddy;
        AccountId accountId = kInvalidAccountId;
    };
    std::map<WatchId, Watch> m_watches;

    // --- call layer state -------------------------------------------------
    //
    // m_calls lock discipline (m_callsMutex):
    //   - PJSUA worker thread INSERTS only (wrapIncomingCall).
    //   - Main thread inserts (makeCall), looks up, and is the SOLE ERASER
    //     (releaseCall, stop). Raw PjsipCall* obtained under the lock on the
    //     main thread therefore stay valid after unlocking — the same
    //     argument CallManager::m_mutex used one layer up.
    //   - NEVER call into PJSIP while holding m_callsMutex — not getInfo,
    //     not hangup/reinvite/answer, and not a pj::Call or
    //     pj::AudioMediaPlayer/Recorder destructor. PJSIP holds its own lock
    //     while dispatching the callbacks that take this mutex; holding it
    //     across a PJSIP call is a lock-order inversion, and some PJSIP
    //     calls (hangup, makeCall) re-enter onCallState synchronously on the
    //     calling thread. Pattern: look up / swap out under the lock,
    //     release it, then talk to PJSIP.
    mutable std::mutex m_callsMutex;
    std::map<CallId, std::unique_ptr<PjsipCall>> m_calls;

    // Grace-parked calls awaiting deferred destruction — main-thread-only.
    // releaseCall() parks here and schedules destruction via
    // m_events.postDelayed(kGraceDestroyMs): destroying a pj::Call
    // immediately is a use-after-free window, because the queued
    // onCallState(Disconnected) that led the manager to releaseCall() may
    // run while the PJSIP thread is still executing the tail of that very
    // callback — pj::Call's destructor does not synchronize with the
    // derived-object reads in that tail (caught live by TSan; this is the
    // relocated CallManager m_graceCalls dance, now adapter-private).
    std::map<CallId, std::unique_ptr<PjsipCall>> m_graceCalls;

    // Per-call WAV recorders and file players — main-thread-only (commands
    // are main-thread; teardown happens in releaseCall / the posted
    // Disconnected lambda / stop, all main-thread). Absorbs CallRecorder
    // and CallManager::m_players.
    std::map<CallId, std::unique_ptr<pj::AudioMediaRecorder>> m_recorders;
    std::map<CallId, std::unique_ptr<pj::AudioMediaPlayer>> m_filePlayers;

    // Device-level ringtone: loops a WAV to the playback device. Absorbs the
    // former sip::RingtonePlayer. Main-thread-only (playRingtone/stopRingtone
    // are main-thread commands); the player is reset in stop()/dtor before the
    // endpoint dies, like the per-call file players above.
    std::unique_ptr<pj::AudioMediaPlayer> m_ringtonePlayer;
    std::string m_ringtonePath;
    bool m_ringtonePlaying = false;

    // Declared LAST: EventDispatch's internal QObject is the invokeMethod
    // context — destroying it cancels undelivered lambdas and pending timers —
    // so it must die before the maps and other state the lambdas may capture.
    // PjsipCall/PjsipAccount destructors (via m_calls/m_accounts.clear()) must
    // serialize against the pjsua lock before m_events dies — see
    // EventDispatch.h's quiesce contract.
    EventDispatch m_events;
};

} // namespace compactphone::sipbackend

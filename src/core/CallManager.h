#pragma once

#include "Account.h"
#include "CallSnapshotSource.h"
#include "TransferTracker.h"

#include <QObject>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace pj {
class AudioMediaPlayer;
}

namespace compactphone::sip {

class AccountsManager;
class CallImpl;
class CallRecorder;
struct CallEntry;

using CallId = std::int32_t;
constexpr CallId kInvalidCallId = -1;

enum class CallState {
    Idle,
    Calling,
    EarlyMedia,
    Confirmed,
    Disconnected,
};

class CallManager : public QObject, public CallSnapshotSource {
    Q_OBJECT
signals:
    // Emitted whenever the set or state of calls changes (added, removed,
    // hold/mute toggled). PhoneController listens to refresh its model.
    void callsChanged();
public:
    explicit CallManager(AccountsManager *am, QObject *parent = nullptr);
    ~CallManager();

    CallManager(const CallManager &) = delete;
    CallManager &operator=(const CallManager &) = delete;

    // Convenience overload — picks the default account.
    CallId makeCall(const std::string &uri);

    // Explicit account selection.
    CallId makeCall(AccountId accountId, const std::string &uri);

    // Bind a PJSIP-native incoming call (id from OnIncomingCallParam) into
    // CallManager's bookkeeping. Auto-declines with 486 Busy Here if any
    // call is already active. Returns the local CallId, or kInvalidCallId
    // if the call was declined or could not be adopted.
    CallId adoptIncomingCall(AccountId accountId, int pjsipCallId);

    bool accept(CallId id);
    bool decline(CallId id);

    // Reject an incoming call with 302 Moved Temporarily and a Contact
    // header pointing at targetUri, so the caller's server redirects.
    bool forwardCall(CallId id, const std::string &targetUri);

    // 3-way conference: cross-connect two confirmed calls' audio paths,
    // unholding heldCallId first. After this both remote parties hear
    // each other and the local user; hanging up one drops the bridge.
    bool mergeCalls(CallId activeCallId, CallId heldCallId);

    // Record a call. Both directions (local mic + remote voice) are mixed
    // into a single WAV at outputPath. Returns true on success.
    bool startRecording(CallId id, const std::string &outputPath);
    bool stopRecording(CallId id);
    bool isRecording(CallId id) const;

    // Transmit a WAV file into the call. PJSIP loops file players by default;
    // pass loop=false to play once. Returns false until call media is active.
    bool playAudioFile(CallId id, const std::string &path, bool loop);
    bool stopAudioFile(CallId id);
    bool isPlayingAudioFile(CallId id) const;

    // Send REFER. Original call self-disconnects on transfer success.
    bool blindTransfer(CallId id, const std::string &targetUri);

    // Send REFER with Replaces. Triggered on the active call; targets a
    // held call's remote. On success both calls disconnect.
    bool attendedTransfer(CallId activeCallId, CallId destCallId);

    void hangup(CallId id);

    // Send re-INVITE with a=sendonly. Returns false if no such call.
    bool hold(CallId id);

    // Send re-INVITE with default flags to resume. Returns false if no such call.
    bool unhold(CallId id);

    // Returns true if the local hold request succeeded and the call has
    // not yet been unheld.
    bool isHeld(CallId id) const;

    // Mute or unmute the local mic for this call. Returns false if no such call
    // or PJSIP rejected the request (e.g., no active audio media).
    bool setMuted(CallId id, bool muted);
    bool isMuted(CallId id) const;

    // Send DTMF digits ("0-9", "*", "#", "A-D"). Uses the DTMF method
    // configured on the originating account (RFC 2833 default, SIP INFO
    // if the account is set to DtmfMethod::Info). Returns false if no
    // such call or PJSIP rejects the request.
    bool sendDtmf(CallId id, const std::string &digits);

    void setOnCallStateChanged(std::function<void(CallState)> cb);
    void setOnCallEvent(std::function<void(CallId, CallState)> cb);

    // Real-time media stats sampled from PJSIP's RTCP report. All fields
    // are -1 when the call has no active stream or stats are not populated.
    struct StreamStats {
        double mos = -1.0;       // estimated MOS 1.0–5.0
        double lossPct = -1.0;   // 0–100 (combined Rx+Tx)
        int rttMs = -1;          // round-trip time
        int jitterMs = -1;       // rxStat jitter mean
    };
    StreamStats streamStats(CallId id) const;

    // Returns the number of active CallImpl entries. Test-only accessor.
    size_t callCount() const;

    // Currently-active call id (the one transmitting audio). kInvalidCallId
    // if no calls are active. Updated by makeCall/accept/unhold/eraseCall.
    CallId activeCallId() const { return m_activeCallId; }

    // Returns a copy of every active call's current state, for QML.
    // Implements CallSnapshotSource so CallsModel can be driven by a fake.
    std::vector<CallEntry> snapshot() const override;

    // Erases the call entry with this id. Called via QMetaObject::invokeMethod
    // on the Qt main thread from CallImpl after DISCONNECTED is dispatched.
    Q_INVOKABLE void eraseCall(int callId);
    Q_INVOKABLE void releaseCallToGrace(int callId);

private:
    friend class CallImpl;

    // Threading contract. PJSIP callbacks arrive on the PJSUA worker thread
    // (SipEngine uses the default uaConfig.threadCnt = 1, mainThreadOnly is
    // not set). On that thread, adoptIncomingCall() inserts into m_calls /
    // m_callAccount and notifyStateChange() writes m_callStates, while the
    // main thread reads and erases the same maps — so those three maps are
    // guarded by m_mutex, and m_nextId is atomic (makeCall on the main thread
    // and adoptIncomingCall on the PJSIP thread both mint ids). Everything
    // else (m_lingeringCalls, m_heldState, m_mutedState, m_transfers, the
    // URI caches, m_players, m_recorder, m_activeCallId) is main-thread-only:
    // CallImpl marshals every other callback to the main thread before it
    // touches them.
    //
    // Lock discipline: NEVER call into PJSIP while holding m_mutex — not
    // getInfo/hangup/reinvite/answer, and not destructors of pj::Call or
    // pj::AudioMediaPlayer (erasing from m_calls/m_players destroys one).
    // PJSIP holds its own lock while dispatching the callbacks that take
    // m_mutex, so holding m_mutex across a PJSIP call is a lock-order
    // inversion; some PJSIP calls (hangup) also re-enter onCallState
    // synchronously on the calling thread. Pattern: look up the raw CallImpl*
    // under the lock, release it, then talk to PJSIP. Raw pointers stay valid
    // after unlocking on the main thread because the main thread is the only
    // eraser of m_calls.
    mutable std::mutex m_mutex;

    AccountsManager *m_am;
    std::unordered_map<CallId, std::unique_ptr<CallImpl>> m_calls;
    struct LingeringCallSnapshot {
        AccountId accountId = kInvalidAccountId;
        std::string remoteUri;
        std::string remoteDisplayName;
        CallState state = CallState::Disconnected;
        bool held = false;
        bool muted = false;
        bool recording = false;
        bool inbound = false;
    };
    std::unordered_map<CallId, LingeringCallSnapshot> m_lingeringCalls;
    std::unordered_map<CallId, AccountId> m_callAccount;
    std::unordered_map<CallId, CallState> m_callStates;
    std::unordered_map<CallId, bool> m_heldState;
    std::unordered_map<CallId, bool> m_mutedState;
    // Tracks calls to hang up once a REFER/transfer completes.
    TransferTracker m_transfers;
    // Cached identity strings — PJSIP clears info.remoteUri after disconnect,
    // but the call card lingers for a grace period, so we remember the URI
    // here while the call is live.
    mutable std::unordered_map<CallId, std::string> m_remoteUriCache;
    mutable std::unordered_map<CallId, std::string> m_remoteDisplayCache;
    // Owns the per-call WAV recorders (a PJSIP conference-bridge slot each;
    // dropped when the call ends or stopRecording is called).
    std::unique_ptr<CallRecorder> m_recorder;
    std::unordered_map<CallId, std::unique_ptr<pj::AudioMediaPlayer>>
        m_players;
    std::function<void(CallState)> m_cb;
    std::function<void(CallId, CallState)> m_eventCb;
    std::atomic<CallId> m_nextId{1};
    CallId m_activeCallId = kInvalidCallId;

    void notifyStateChange(CallId id, CallState s);
    void handleTransferStatus(CallId id, int statusCode, bool finalNotify,
                              const std::string &reason);
    bool isConfirmedState(CallId id) const;
    bool requestUnhold(CallId id, int retriesRemaining);
    bool wireBridge(CallId activeCallId, CallId heldCallId, int retriesRemaining);
    void cleanupTransferredCalls(CallId transferCallId);

    // Promotes `id` to active. Auto-holds the previously active call (if any
    // and not already held), and unholds `id` if it was held.
    void setActiveCall(CallId id);
};

} // namespace compactphone::sip

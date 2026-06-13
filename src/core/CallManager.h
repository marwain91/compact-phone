#pragma once

#include "Account.h"
#include "CallSnapshotSource.h"
#include "TransferTracker.h"
#include "sipbackend/ISipBackend.h"

#include <QObject>

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace compactphone::sip {

class AccountsManager;
struct CallEntry;

using CallId = std::int32_t;
constexpr CallId kInvalidCallId = -1;

// CallManager speaks the boundary call-state enum directly — sip::CallState
// is an alias, not a parallel type, so there is no value-mapping seam.
using CallState = sipbackend::CallState;

// CallManager is the stack-neutral call POLICY layer: active-call
// auto-hold, 486 decline semantics, transfer-NOTIFY outcome handling
// (TransferTracker), unhold/bridge retries, and the post-disconnect UI
// linger. All media/protocol MECHANICS live behind ISipBackend.
//
// THREADING: main-thread-only. Commands go to the backend on the main
// thread (boundary rule 1); events arrive queued on the main thread
// (boundary rule 2). There is no cross-thread state left and no mutex —
// the locking discipline that used to live here moved into the PJSIP
// adapter, where it is private. State changes are published as Qt signals
// (callEvent/callStateChanged), connected directly on the main thread.
//
// CallId values are the backend-minted ids — CallManager performs no id
// translation. An id is live from makeCall()/onIncomingCall until the
// manager calls m_backend->releaseCall(id) while handling Disconnected.
class CallManager : public QObject,
                    public CallSnapshotSource,
                    public sipbackend::ISipBackendListener {
    Q_OBJECT
signals:
    // Emitted whenever the set or state of calls changes (added, removed,
    // hold/mute toggled). PhoneController listens to refresh its model.
    void callsChanged();
    // Emitted on the main thread when an incoming call has been recorded
    // (replaces AccountsManager::setOnIncomingCall + adoptIncomingCall:
    // the adapter wraps the call eagerly inside the PJSIP callback, so by
    // the time this fires, accept()/decline() are valid on the id).
    void incomingCall(int callId);
    // Emitted on every call state transition. callStateChanged carries the
    // new state alone (a convenient observer; the integration suite uses it);
    // callEvent additionally identifies the call. Both fire on the main
    // thread from notifyStateChange — connect them directly (no metatype
    // registration needed). Production wiring (CallsController, headless)
    // uses callEvent.
    void callStateChanged(CallState s);
    void callEvent(CallId id, CallState s);

public:
    CallManager(sipbackend::ISipBackend *backend, AccountsManager *am,
                QObject *parent = nullptr);
    ~CallManager() override;

    CallManager(const CallManager &) = delete;
    CallManager &operator=(const CallManager &) = delete;

    // Convenience overload — picks the default account.
    CallId makeCall(const std::string &uri);

    // Explicit account selection.
    CallId makeCall(AccountId accountId, const std::string &uri);

    bool accept(CallId id);

    // Rejects a ringing incoming call with 486 Busy Here, so the server
    // applies its busy treatment (forward-on-busy, voicemail) instead of
    // hard-failing the caller. Used by both the manual decline button and
    // the DND policy.
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

    // Send REFER. The call stays up until the transfer's final NOTIFY
    // reports the outcome: a 2xx hangs up the originating call; a failure
    // (busy, declined, unknown target) keeps it so the user can resume
    // talking. A server that never sends the final NOTIFY leaves the call
    // up for the user to hang up manually.
    bool blindTransfer(CallId id, const std::string &targetUri);

    // Send REFER with Replaces. Triggered on the active call; targets a
    // held call's remote. Both legs stay up until the final NOTIFY: a 2xx
    // hangs them both up, a failure keeps them.
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
    // or the backend rejected the request.
    bool setMuted(CallId id, bool muted);
    bool isMuted(CallId id) const;

    // Send DTMF digits ("0-9", "*", "#", "A-D"). Uses the DTMF method
    // configured on the originating account (RFC 2833 default, SIP INFO
    // if the account is set to DtmfMethod::Info). Returns false if no
    // such call or the backend rejects the request.
    bool sendDtmf(CallId id, const std::string &digits);

    // Real-time media stats sampled from the backend's RTCP report. All
    // fields are -1 when the call has no active stream or stats are not
    // populated.
    struct StreamStats {
        double mos = -1.0;       // estimated MOS 1.0–5.0
        double lossPct = -1.0;   // 0–100 (combined Rx+Tx)
        int rttMs = -1;          // round-trip time
        int jitterMs = -1;       // rxStat jitter mean
    };
    StreamStats streamStats(CallId id) const;

    // Returns the number of live call records. Test-only accessor.
    size_t callCount() const;

    // Returns true if the local capture device currently transmits into this
    // call's active audio media — i.e. the microphone is live for this call
    // in the backend's conference bridge, regardless of what isMuted() claims.
    // Test-only accessor: lets tests pin "muted means the mic is actually
    // disconnected", which isMuted() (bookkeeping only) cannot. Note the
    // bridge applies connect/disconnect on the audio-clock tick, so this view
    // can lag a setMuted() call by a few milliseconds — tests must poll rather
    // than assert immediately after a transition.
    bool isCaptureTransmitting(CallId id) const;

    // Returns true if any of the call's audio media is in the ACTIVE state.
    // Test-only accessor: hold parks media in LOCAL_HOLD and unhold brings it
    // back to ACTIVE, so this is the observable that a locally-initiated
    // re-INVITE actually completed (got its final response and renegotiated),
    // which isHeld() (bookkeeping only) cannot show.
    bool isMediaActive(CallId id) const;

    // SIP status code of the call's final disposition — e.g. 486 for a
    // decline/busy, 200 for a normal BYE. Returns 0 while the call is still
    // up or if the id is unknown. The code rides the Disconnected event, so
    // it is readable the moment Disconnected is observed and stays readable
    // through the post-disconnect linger window. Test-only accessor: lets
    // tests assert WHY a call ended (a DND decline vs. any other teardown),
    // which CallState::Disconnected alone cannot show.
    int lastStatusCode(CallId id) const;

    // Currently-active call id (the one transmitting audio). kInvalidCallId
    // if no calls are active. Updated by makeCall/accept/unhold/disconnect.
    CallId activeCallId() const { return m_activeCallId; }

    // Returns a copy of every active call's current state, for QML.
    // Implements CallSnapshotSource so CallsModel can be driven by a fake.
    std::vector<CallEntry> snapshot() const override;

    // Test seam: shortens the post-disconnect UI linger (default 2200 ms)
    // so unit tests don't sleep through the production grace window.
    void setLingerMsForTest(int ms) { m_lingerMs = ms; }

    // --- ISipBackendListener (call half; account events left defaulted) ---
    void onIncomingCall(sipbackend::AccountId backendAccId,
                        sipbackend::CallId callId,
                        const std::string &remoteUri,
                        const std::string &displayName) override;
    void onCallState(sipbackend::CallId id, sipbackend::CallState s,
                     int sipCode) override;
    // Deliberately unused in phase 3: held/muted UI state is command-driven
    // bookkeeping, and media-truth assertions use the synchronous
    // isMediaActive()/isCaptureTransmitting() pulls. Kept as an explicit
    // empty override so the omission reads as a decision, not an accident.
    void onMediaState(sipbackend::CallId, bool, bool) override {}
    void onTransferStatus(sipbackend::CallId id, int sipCode, bool isFinal,
                          const std::string &reason) override;

private:
    struct CallRecord {
        AccountId accountId = kInvalidAccountId;   // domain account id
        std::string remoteUri;          // dialed target (outbound) /
                                        // pushed remote URI (inbound)
        std::string remoteDisplayName;  // pushed (inbound); empty outbound
        CallState state = CallState::Calling;
        int lastStatusCode = 0;         // final disposition, set on Disconnected
        bool inbound = false;
        bool held = false;
        bool muted = false;
        bool recording = false;
        bool playingFile = false;
    };

    sipbackend::ISipBackend *m_backend;
    AccountsManager *m_am;

    // Live calls — main-thread-only.
    std::unordered_map<CallId, CallRecord> m_records;
    // Post-disconnect UI linger (frozen records) — main-thread-only. A
    // lingering entry is just a frozen record with state == Disconnected.
    std::unordered_map<CallId, CallRecord> m_lingeringCalls;
    // Tracks calls to hang up once a REFER/transfer completes.
    TransferTracker m_transfers;

    CallId m_activeCallId = kInvalidCallId;
    int m_lingerMs = 2200;

    void notifyStateChange(CallId id, CallState s);
    void handleDisconnected(CallId id, int sipCode);
    void eraseLingering(CallId id);
    void handleTransferStatus(CallId id, int statusCode, bool finalNotify,
                              const std::string &reason);
    bool isConfirmedState(CallId id) const;
    bool requestUnhold(CallId id, int retriesRemaining);
    bool wireBridge(CallId activeCallId, CallId heldCallId, int retriesRemaining);
    // Hangs up each recorded transfer leg; legs that already disconnected on
    // their own are skipped. Main-thread-only.
    void hangupTransferLegs(const std::vector<CallId> &cleanupIds);

    // Promotes `id` to active. Auto-holds the previously active call (if any
    // and not already held), and unholds `id` if it was held.
    void setActiveCall(CallId id);
};

} // namespace compactphone::sip

#pragma once

// The SIP stack boundary. Everything below this interface is a backend
// (the PJSIP adapter, or the test fake); everything above is
// stack-neutral domain code. Threading contract (see spec §Threading):
//
//  1. Every ISipBackend method is called from the Qt main thread only.
//  2. Every ISipBackendListener method is delivered QUEUED on the Qt
//     main thread — never synchronously from inside a backend method
//     (no re-entrancy, ever). Per-backend event order is preserved.
//  3. A CallId is live from makeCall() return / onIncomingCall delivery
//     until releaseCall(). Backends defer real teardown internally.
//  4. After stop() returns no listener invocation is in flight and none
//     will be delivered.
//  5. Teardown emits only onCallState(Disconnected); media inactivity is
//     implied. Backends must not emit a separate onMediaState event when
//     a call ends.

#include "Types.h"

#include <functional>
#include <string>
#include <vector>

namespace compactphone::sipbackend {

class ISipBackendListener {
public:
    virtual ~ISipBackendListener() = default;

    // Registration result for one REGISTER event; feeds sip::mapRegEvent.
    virtual void onRegState(AccountId, bool regActive, int sipCode,
                            const std::string &reason) {}
    // Data is pushed (remote URI, display name) — managers never query
    // back from inside an event.
    virtual void onIncomingCall(AccountId, CallId,
                                const std::string &remoteUri,
                                const std::string &displayName) {}
    virtual void onCallState(CallId, CallState, int sipCode) {}
    virtual void onMediaState(CallId, bool active, bool held) {}
    virtual void onTransferStatus(CallId, int sipCode, bool isFinal,
                                  const std::string &reason) {}
    virtual void onMwi(AccountId, int newMessages, int oldMessages,
                       bool active) {}
    virtual void onInstantMessage(AccountId, const std::string &fromUri,
                                  const std::string &body) {}
    virtual void onPresence(WatchId, PresenceState) {}
};

class ISipBackend {
public:
    virtual ~ISipBackend() = default;

    // --- lifecycle ---
    // Backends must tolerate a null listener (events are dropped). Set the
    // listener before start() to receive events; it survives until cleared.
    // setListener(nullptr) is a quiesce barrier: after it returns, no
    // event is delivered to the previous listener.
    virtual void setListener(ISipBackendListener *listener) = 0;
    virtual bool start(const EngineConfig &cfg) = 0;
    // stop() drops all accounts, calls, and watches — a restarted backend
    // starts empty.
    virtual void stop() = 0;
    virtual bool isRunning() const = 0;

    // --- engine-level config (semantics of today's SipEngine) ---
    virtual void setCaTrust(const CaTrust &trust) = 0;
    virtual void setStunServers(const std::vector<std::string> &servers) = 0;
    virtual void setCodecPriority(
        const std::vector<std::string> &priorityOrder) = 0;
    virtual std::vector<AudioDevice> audioDevices() const = 0;
    virtual int captureDevice() const = 0;
    virtual int playbackDevice() const = 0;
    virtual bool setCaptureDevice(int id) = 0;
    virtual bool setPlaybackDevice(int id) = 0;
    virtual void refreshAudioDevices() = 0;
    virtual bool playRingtone(const std::string &path) = 0;
    virtual void stopRingtone() = 0;
    virtual void setLogSink(
        std::function<void(int level, const std::string &msg)> sink) = 0;

    // --- accounts ---
    virtual AccountId addAccount(const AccountSettings &settings) = 0;
    virtual bool removeAccount(AccountId id) = 0;
    virtual bool sendMessage(AccountId id, const std::string &toUri,
                             const std::string &body) = 0;

    // --- presence ---
    virtual WatchId watch(AccountId accountId, const std::string &uri) = 0;
    virtual bool unwatch(WatchId id) = 0;

    // --- calls (mirrors CallManager's op set 1:1) ---
    virtual CallId makeCall(AccountId accountId, const std::string &uri) = 0;
    virtual bool answer(CallId id) = 0;
    virtual bool decline(CallId id, int sipCode) = 0;
    virtual bool redirect(CallId id, const std::string &contactUri) = 0;
    virtual void hangup(CallId id) = 0;
    virtual bool hold(CallId id) = 0;
    virtual bool unhold(CallId id) = 0;
    virtual bool setMuted(CallId id, bool muted) = 0;
    virtual bool sendDtmf(CallId id, const std::string &digits,
                          DtmfMethod method) = 0;
    virtual bool blindTransfer(CallId id, const std::string &targetUri) = 0;
    virtual bool attendedTransfer(CallId id, CallId otherId) = 0;
    virtual bool bridge(CallId id, CallId otherId) = 0;
    virtual bool startRecording(CallId id, const std::string &path) = 0;
    virtual bool stopRecording(CallId id) = 0;
    virtual bool playFile(CallId id, const std::string &path, bool loop) = 0;
    virtual bool stopFile(CallId id) = 0;
    virtual StreamStats streamStats(CallId id) const = 0;
    virtual bool isMediaActive(CallId id) const = 0;
    virtual bool isCaptureTransmitting(CallId id) const = 0;
    virtual void releaseCall(CallId id) = 0;
};

} // namespace compactphone::sipbackend

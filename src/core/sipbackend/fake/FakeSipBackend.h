#pragma once

// Scriptable in-memory ISipBackend for unit tests and headless use.
// Implements the full interface with deterministic bookkeeping; the
// simulate*() methods play the remote side. Events follow the boundary
// contract: queued onto the Qt main thread, dropped after stop() /
// setListener(nullptr).
//
// Main-thread-only, like every backend consumer. The epoch counter is
// how queued lambdas detect they were invalidated: stop() and
// setListener() bump it, and a lambda only fires if its captured epoch
// still matches. Destruction safety comes from m_dispatch being the
// invokeMethod context — destroying it cancels undelivered lambdas —
// so it must never be replaced with an app-global context.

#include "../ISipBackend.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

class QObject;

namespace compactphone::sipbackend {

class FakeSipBackend : public ISipBackend {
public:
    FakeSipBackend();
    ~FakeSipBackend() override;

    FakeSipBackend(const FakeSipBackend &) = delete;
    FakeSipBackend &operator=(const FakeSipBackend &) = delete;

    // --- ISipBackend ---
    void setListener(ISipBackendListener *listener) override;
    bool start(const EngineConfig &cfg) override;
    void stop() override;
    bool isRunning() const override { return m_running; }

    void setCaTrust(const CaTrust &trust) override { m_caTrust = trust; }
    void setStunServers(const std::vector<std::string> &servers) override
    {
        m_stunServers = servers;
    }
    void setCodecPriority(
        const std::vector<std::string> &priorityOrder) override
    {
        m_codecPriority = priorityOrder;
    }
    std::vector<AudioDevice> audioDevices() const override;
    int captureDevice() const override { return m_captureDevice; }
    int playbackDevice() const override { return m_playbackDevice; }
    bool setCaptureDevice(int id) override;
    bool setPlaybackDevice(int id) override;
    void refreshAudioDevices() override {}
    bool playRingtone(const std::string &path) override;
    void stopRingtone() override { m_ringtonePath.clear(); }
    void setLogSink(
        std::function<void(int, const std::string &)> sink) override
    {
        m_logSink = std::move(sink);
    }

    AccountId addAccount(const AccountSettings &settings) override;
    bool removeAccount(AccountId id) override;
    bool sendMessage(AccountId id, const std::string &toUri,
                     const std::string &body) override;

    WatchId watch(AccountId accountId, const std::string &uri) override;
    bool unwatch(WatchId id) override;

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

    // --- test scripting: the remote side ---
    void simulateRegState(AccountId id, bool regActive, int sipCode,
                          const std::string &reason);
    // Returns the backend-minted CallId delivered via onIncomingCall.
    CallId simulateIncomingCall(AccountId accountId,
                                const std::string &remoteUri,
                                const std::string &displayName);
    void simulateRemoteAnswer(CallId id);
    void simulateRemoteHangup(CallId id, int sipCode);
    void simulateTransferStatus(CallId id, int sipCode, bool isFinal,
                                const std::string &reason);
    void simulateMwi(AccountId id, int newMessages, int oldMessages,
                     bool active);
    void simulateInstantMessage(AccountId id, const std::string &fromUri,
                                const std::string &body);
    void simulatePresence(WatchId id, PresenceState state);

    // --- test inspection ---
    struct FakeCall {
        AccountId accountId = kInvalidAccountId;
        std::string remoteUri;
        CallState state = CallState::Idle;
        bool inbound = false;
        bool held = false;
        bool muted = false;
        bool mediaActive = false;
        bool recording = false;
        bool playingFile = false;
        bool released = false;
        std::string dtmfSent;
    };
    // Empty optional-like: callExists() guards lookups.
    bool callExists(CallId id) const { return m_calls.count(id) != 0; }
    const FakeCall &callInfo(CallId id) const { return m_calls.at(id); }
    size_t accountCount() const { return m_accounts.size(); }
    // Every mutating backend command, in order ("makeCall:1:sip:200@x",
    // "hold:1", ...). Lets tests assert managers issued the right ops.
    const std::vector<std::string> &commandLog() const { return m_log; }

private:
    struct FakeAccount {
        AccountSettings settings;
    };

    void post(std::function<void()> fn);
    void logCmd(const std::string &line);
    FakeCall *liveCall(CallId id);

    bool m_running = false;
    std::uint64_t m_epoch = 0;
    ISipBackendListener *m_listener = nullptr;
    std::unique_ptr<QObject> m_dispatch;

    CaTrust m_caTrust;
    std::vector<std::string> m_stunServers;
    std::vector<std::string> m_codecPriority;
    int m_captureDevice = 0;
    int m_playbackDevice = 0;
    std::string m_ringtonePath;
    std::function<void(int, const std::string &)> m_logSink;

    AccountId m_nextAccountId = 1;
    CallId m_nextCallId = 1;
    WatchId m_nextWatchId = 1;
    std::map<AccountId, FakeAccount> m_accounts;
    std::map<CallId, FakeCall> m_calls;
    std::map<WatchId, std::string> m_watches;
    std::vector<std::string> m_log;
};

} // namespace compactphone::sipbackend

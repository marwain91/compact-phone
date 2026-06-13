#pragma once

// Scriptable in-memory ISipBackend for unit tests and headless use.
// Implements the full interface with deterministic bookkeeping; the
// simulate*() methods play the remote side. Events follow the boundary
// contract: queued onto the Qt main thread, dropped after stop() /
// setListener(nullptr).
//
// Main-thread-only, like every backend consumer. Event queuing and
// invalidation are handled by EventDispatch (see EventDispatch.h):
// stop() and setListener() call m_events.invalidate(), which bumps
// the epoch so lambdas queued before it are dropped. Destruction safety
// comes from EventDispatch::m_dispatch being the invokeMethod context —
// destroying it cancels undelivered lambdas — so m_events is declared
// LAST among data members so it dies first.

#include "../ISipBackend.h"
#include "../EventDispatch.h"

#include <functional>
#include <map>
#include <memory>
#include <string>
#include <vector>

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
    // Test-only: the path passed to the last playRingtone(), or empty after
    // stopRingtone(). Lets a SettingsController test observe ringtone state.
    const std::string &ringtonePath() const { return m_ringtonePath; }
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
        std::string dtmfSent;
    };
    // Empty optional-like: callExists() guards lookups.
    bool callExists(CallId id) const { return m_calls.count(id) != 0; }
    const FakeCall &callInfo(CallId id) const { return m_calls.at(id); }
    size_t accountCount() const { return m_accounts.size(); }
    // Returns the backend AccountId assigned to the most recently added
    // account (the value returned by the last addAccount() call).  Returns
    // kInvalidAccountId if no account has been added yet.
    AccountId lastAddedAccountId() const { return m_lastAddedId; }
    // Account, watch, and call commands the consumer issued, in order
    // ("addAccount:1:alice", "makeCall:1:sip:200@x", "hold:1", ...).
    // Engine-level config setters are not logged.
    const std::vector<std::string> &commandLog() const { return m_log; }

private:
    struct FakeAccount {
        AccountSettings settings;
    };

    void post(std::function<void()> fn);
    void logCmd(const std::string &line);
    FakeCall *liveCall(CallId id);

    bool m_running = false;
    ISipBackendListener *m_listener = nullptr;

    CaTrust m_caTrust;
    std::vector<std::string> m_stunServers;
    std::vector<std::string> m_codecPriority;
    int m_captureDevice = 0;
    int m_playbackDevice = 0;
    std::string m_ringtonePath;
    std::function<void(int, const std::string &)> m_logSink;

    AccountId m_nextAccountId = 1;
    AccountId m_lastAddedId = kInvalidAccountId;
    CallId m_nextCallId = 1;
    WatchId m_nextWatchId = 1;
    std::map<AccountId, FakeAccount> m_accounts;
    std::map<CallId, FakeCall> m_calls;
    std::map<WatchId, std::string> m_watches;
    std::vector<std::string> m_log;

    // Declared LAST: EventDispatch's internal QObject is the invokeMethod
    // context — destroying it cancels undelivered lambdas — so it must die
    // before the maps and other state the lambdas may capture.
    EventDispatch m_events;
};

} // namespace compactphone::sipbackend

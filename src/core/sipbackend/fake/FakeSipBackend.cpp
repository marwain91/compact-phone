#include "FakeSipBackend.h"

#include <QObject>

namespace compactphone::sipbackend {

FakeSipBackend::FakeSipBackend()
    : m_dispatch(std::make_unique<QObject>())
{
}

FakeSipBackend::~FakeSipBackend()
{
    stop();
}

void FakeSipBackend::setListener(ISipBackendListener *listener)
{
    // Quiesce barrier: bump the epoch so lambdas queued for the previous
    // listener no-op when they run.
    ++m_epoch;
    m_listener = listener;
}

bool FakeSipBackend::start(const EngineConfig &cfg)
{
    (void)cfg;
    m_running = true;
    return true;
}

void FakeSipBackend::stop()
{
    // Contract rule 4: nothing queued before stop() may fire after it.
    ++m_epoch;
    m_running = false;
    // Per ISipBackend contract: a restarted backend starts empty.
    m_accounts.clear();
    m_calls.clear();
    m_watches.clear();
    m_ringtonePath.clear();
}

void FakeSipBackend::post(std::function<void()> fn)
{
    const auto epoch = m_epoch;
    QMetaObject::invokeMethod(
        m_dispatch.get(),
        [this, epoch, fn = std::move(fn)] {
            if (epoch == m_epoch && m_listener)
                fn();
        },
        Qt::QueuedConnection);
}

void FakeSipBackend::logCmd(const std::string &line)
{
    m_log.push_back(line);
}

std::vector<AudioDevice> FakeSipBackend::audioDevices() const
{
    return {
        {0, "Fake Microphone", 1, 0},
        {1, "Fake Speakers", 0, 2},
    };
}

bool FakeSipBackend::setCaptureDevice(int id)
{
    const auto devs = audioDevices();
    for (const auto &d : devs) {
        if (d.id == id && d.inputCount > 0) {
            m_captureDevice = id;
            return true;
        }
    }
    return false;
}

bool FakeSipBackend::setPlaybackDevice(int id)
{
    const auto devs = audioDevices();
    for (const auto &d : devs) {
        if (d.id == id && d.outputCount > 0) {
            m_playbackDevice = id;
            return true;
        }
    }
    return false;
}

bool FakeSipBackend::playRingtone(const std::string &path)
{
    if (!m_running)
        return false;
    m_ringtonePath = path;
    return true;
}

AccountId FakeSipBackend::addAccount(const AccountSettings &settings)
{
    if (!m_running)
        return kInvalidAccountId;
    const AccountId id = m_nextAccountId++;
    m_accounts[id] = FakeAccount{settings};
    logCmd("addAccount:" + std::to_string(id) + ":" + settings.username);
    return id;
}

bool FakeSipBackend::removeAccount(AccountId id)
{
    if (m_accounts.erase(id) == 0)
        return false;
    logCmd("removeAccount:" + std::to_string(id));
    return true;
}

bool FakeSipBackend::sendMessage(AccountId id, const std::string &toUri,
                                 const std::string &body)
{
    if (!m_running || m_accounts.count(id) == 0)
        return false;
    logCmd("sendMessage:" + std::to_string(id) + ":" + toUri + ":" + body);
    return true;
}

WatchId FakeSipBackend::watch(AccountId accountId, const std::string &uri)
{
    if (!m_running || m_accounts.count(accountId) == 0)
        return kInvalidWatchId;
    const WatchId id = m_nextWatchId++;
    m_watches[id] = uri;
    logCmd("watch:" + std::to_string(id) + ":" + uri);
    return id;
}

bool FakeSipBackend::unwatch(WatchId id)
{
    if (m_watches.erase(id) == 0)
        return false;
    logCmd("unwatch:" + std::to_string(id));
    return true;
}

void FakeSipBackend::simulateRegState(AccountId id, bool regActive,
                                      int sipCode, const std::string &reason)
{
    post([this, id, regActive, sipCode, reason] {
        m_listener->onRegState(id, regActive, sipCode, reason);
    });
}

void FakeSipBackend::simulateMwi(AccountId id, int newMessages,
                                 int oldMessages, bool active)
{
    post([this, id, newMessages, oldMessages, active] {
        m_listener->onMwi(id, newMessages, oldMessages, active);
    });
}

void FakeSipBackend::simulateInstantMessage(AccountId id,
                                            const std::string &fromUri,
                                            const std::string &body)
{
    post([this, id, fromUri, body] {
        m_listener->onInstantMessage(id, fromUri, body);
    });
}

void FakeSipBackend::simulatePresence(WatchId id, PresenceState state)
{
    post([this, id, state] { m_listener->onPresence(id, state); });
}

FakeSipBackend::FakeCall *FakeSipBackend::liveCall(CallId id)
{
    auto it = m_calls.find(id);
    if (it == m_calls.end())
        return nullptr;
    return &it->second;
}

CallId FakeSipBackend::makeCall(AccountId accountId, const std::string &uri)
{
    if (!m_running || m_accounts.count(accountId) == 0)
        return kInvalidCallId;
    const CallId id = m_nextCallId++;
    FakeCall c;
    c.accountId = accountId;
    c.remoteUri = uri;
    c.state = CallState::Calling;
    m_calls[id] = c;
    logCmd("makeCall:" + std::to_string(id) + ":" + uri);
    post([this, id] {
        m_listener->onCallState(id, CallState::Calling, 0);
    });
    return id;
}

CallId FakeSipBackend::simulateIncomingCall(AccountId accountId,
                                            const std::string &remoteUri,
                                            const std::string &displayName)
{
    if (!m_running || m_accounts.count(accountId) == 0)
        return kInvalidCallId;
    const CallId id = m_nextCallId++;
    FakeCall c;
    c.accountId = accountId;
    c.remoteUri = remoteUri;
    c.state = CallState::EarlyMedia;
    c.inbound = true;
    m_calls[id] = c;
    post([this, accountId, id, remoteUri, displayName] {
        m_listener->onIncomingCall(accountId, id, remoteUri, displayName);
    });
    return id;
}

bool FakeSipBackend::answer(CallId id)
{
    auto *c = liveCall(id);
    if (!c || !c->inbound || c->state != CallState::EarlyMedia)
        return false;
    logCmd("answer:" + std::to_string(id));
    c->state = CallState::Confirmed;
    c->mediaActive = true;
    post([this, id] {
        m_listener->onCallState(id, CallState::Confirmed, 200);
        m_listener->onMediaState(id, true, false);
    });
    return true;
}

void FakeSipBackend::simulateRemoteAnswer(CallId id)
{
    auto *c = liveCall(id);
    if (!c || c->inbound || c->state != CallState::Calling)
        return;
    c->state = CallState::Confirmed;
    c->mediaActive = true;
    post([this, id] {
        m_listener->onCallState(id, CallState::Confirmed, 200);
        m_listener->onMediaState(id, true, false);
    });
}

bool FakeSipBackend::decline(CallId id, int sipCode)
{
    auto *c = liveCall(id);
    if (!c || !c->inbound || c->state != CallState::EarlyMedia)
        return false;
    logCmd("decline:" + std::to_string(id) + ":" + std::to_string(sipCode));
    c->state = CallState::Disconnected;
    post([this, id, sipCode] {
        m_listener->onCallState(id, CallState::Disconnected, sipCode);
    });
    return true;
}

bool FakeSipBackend::redirect(CallId id, const std::string &contactUri)
{
    auto *c = liveCall(id);
    if (!c || !c->inbound || c->state != CallState::EarlyMedia)
        return false;
    logCmd("redirect:" + std::to_string(id) + ":" + contactUri);
    c->state = CallState::Disconnected;
    post([this, id] {
        m_listener->onCallState(id, CallState::Disconnected, 302);
    });
    return true;
}

void FakeSipBackend::hangup(CallId id)
{
    auto *c = liveCall(id);
    if (!c || c->state == CallState::Disconnected)
        return;
    // 487 Request Terminated = pre-answer cancel; 200 = normal teardown of
    // an established call. Capture before mutating state.
    const int code = (c->state == CallState::Confirmed) ? 200 : 487;
    logCmd("hangup:" + std::to_string(id));
    c->state = CallState::Disconnected;
    c->mediaActive = false;
    post([this, id, code] {
        m_listener->onCallState(id, CallState::Disconnected, code);
    });
}

void FakeSipBackend::simulateRemoteHangup(CallId id, int sipCode)
{
    auto *c = liveCall(id);
    if (!c || c->state == CallState::Disconnected)
        return;
    c->state = CallState::Disconnected;
    c->mediaActive = false;
    post([this, id, sipCode] {
        m_listener->onCallState(id, CallState::Disconnected, sipCode);
    });
}

bool FakeSipBackend::hold(CallId id)
{
    auto *c = liveCall(id);
    if (!c || c->state != CallState::Confirmed || c->held)
        return false;
    logCmd("hold:" + std::to_string(id));
    c->held = true;
    c->mediaActive = false;
    post([this, id] { m_listener->onMediaState(id, false, true); });
    return true;
}

bool FakeSipBackend::unhold(CallId id)
{
    auto *c = liveCall(id);
    if (!c || c->state != CallState::Confirmed || !c->held)
        return false;
    logCmd("unhold:" + std::to_string(id));
    c->held = false;
    c->mediaActive = true;
    post([this, id] { m_listener->onMediaState(id, true, false); });
    return true;
}

bool FakeSipBackend::setMuted(CallId id, bool muted)
{
    auto *c = liveCall(id);
    if (!c || c->state != CallState::Confirmed)
        return false;
    logCmd("setMuted:" + std::to_string(id) + ":" + (muted ? "1" : "0"));
    c->muted = muted;
    return true;
}

bool FakeSipBackend::sendDtmf(CallId id, const std::string &digits,
                              DtmfMethod method)
{
    (void)method;
    auto *c = liveCall(id);
    if (!c || c->state != CallState::Confirmed)
        return false;
    logCmd("sendDtmf:" + std::to_string(id) + ":" + digits);
    c->dtmfSent += digits;
    return true;
}

bool FakeSipBackend::blindTransfer(CallId id, const std::string &targetUri)
{
    auto *c = liveCall(id);
    if (!c || c->state != CallState::Confirmed)
        return false;
    logCmd("blindTransfer:" + std::to_string(id) + ":" + targetUri);
    return true;
}

bool FakeSipBackend::attendedTransfer(CallId id, CallId otherId)
{
    auto *a = liveCall(id);
    auto *b = liveCall(otherId);
    // Both legs must be Confirmed: the transfer target is a held (Confirmed)
    // call, and the transferring leg must also be active (Confirmed).
    if (!a || !b || a->state != CallState::Confirmed
        || b->state != CallState::Confirmed)
        return false;
    logCmd("attendedTransfer:" + std::to_string(id) + ":"
           + std::to_string(otherId));
    return true;
}

bool FakeSipBackend::bridge(CallId id, CallId otherId)
{
    auto *a = liveCall(id);
    auto *b = liveCall(otherId);
    // Both legs must be Confirmed before bridging.
    if (!a || !b || a->state != CallState::Confirmed
        || b->state != CallState::Confirmed)
        return false;
    logCmd("bridge:" + std::to_string(id) + ":" + std::to_string(otherId));
    return true;
}

bool FakeSipBackend::startRecording(CallId id, const std::string &path)
{
    auto *c = liveCall(id);
    // Confirmed (not mediaActive) guard: record-arm on a held call is allowed.
    if (!c || c->state != CallState::Confirmed || c->recording)
        return false;
    logCmd("startRecording:" + std::to_string(id) + ":" + path);
    c->recording = true;
    return true;
}

bool FakeSipBackend::stopRecording(CallId id)
{
    auto *c = liveCall(id);
    if (!c || !c->recording)
        return false;
    logCmd("stopRecording:" + std::to_string(id));
    c->recording = false;
    return true;
}

bool FakeSipBackend::playFile(CallId id, const std::string &path, bool loop)
{
    (void)loop;
    auto *c = liveCall(id);
    // mediaActive guard (not Confirmed): playing audio requires an active
    // media session; it is not meaningful on a held call.
    if (!c || !c->mediaActive)
        return false;
    logCmd("playFile:" + std::to_string(id) + ":" + path);
    c->playingFile = true;
    return true;
}

bool FakeSipBackend::stopFile(CallId id)
{
    auto *c = liveCall(id);
    if (!c || !c->playingFile)
        return false;
    logCmd("stopFile:" + std::to_string(id));
    c->playingFile = false;
    return true;
}

StreamStats FakeSipBackend::streamStats(CallId id) const
{
    auto it = m_calls.find(id);
    if (it == m_calls.end() || !it->second.mediaActive)
        return {};
    StreamStats s;
    s.mos = 4.2;
    s.lossPct = 0.0;
    s.rttMs = 20;
    s.jitterMs = 3;
    return s;
}

bool FakeSipBackend::isMediaActive(CallId id) const
{
    auto it = m_calls.find(id);
    return it != m_calls.end() && it->second.mediaActive;
}

bool FakeSipBackend::isCaptureTransmitting(CallId id) const
{
    auto it = m_calls.find(id);
    return it != m_calls.end() && it->second.mediaActive && !it->second.muted;
}

void FakeSipBackend::releaseCall(CallId id)
{
    if (m_calls.erase(id) != 0)
        logCmd("releaseCall:" + std::to_string(id));
}

void FakeSipBackend::simulateTransferStatus(CallId id, int sipCode,
                                            bool isFinal,
                                            const std::string &reason)
{
    if (!liveCall(id))
        return;
    post([this, id, sipCode, isFinal, reason] {
        m_listener->onTransferStatus(id, sipCode, isFinal, reason);
    });
}

} // namespace compactphone::sipbackend

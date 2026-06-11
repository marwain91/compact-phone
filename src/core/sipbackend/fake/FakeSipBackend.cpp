#include "FakeSipBackend.h"

#include <QCoreApplication>
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
    if (id != 0 && id != 1)
        return false;
    m_captureDevice = id;
    return true;
}

bool FakeSipBackend::setPlaybackDevice(int id)
{
    if (id != 0 && id != 1)
        return false;
    m_playbackDevice = id;
    return true;
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
    logCmd("removeAccount:" + std::to_string(id));
    return m_accounts.erase(id) != 0;
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
    logCmd("unwatch:" + std::to_string(id));
    return m_watches.erase(id) != 0;
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

// --- Task 4 replaces everything below with real call bookkeeping ---
CallId FakeSipBackend::makeCall(AccountId, const std::string &) { return kInvalidCallId; }
bool FakeSipBackend::answer(CallId) { return false; }
bool FakeSipBackend::decline(CallId, int) { return false; }
bool FakeSipBackend::redirect(CallId, const std::string &) { return false; }
void FakeSipBackend::hangup(CallId) {}
bool FakeSipBackend::hold(CallId) { return false; }
bool FakeSipBackend::unhold(CallId) { return false; }
bool FakeSipBackend::setMuted(CallId, bool) { return false; }
bool FakeSipBackend::sendDtmf(CallId, const std::string &, DtmfMethod) { return false; }
bool FakeSipBackend::blindTransfer(CallId, const std::string &) { return false; }
bool FakeSipBackend::attendedTransfer(CallId, CallId) { return false; }
bool FakeSipBackend::bridge(CallId, CallId) { return false; }
bool FakeSipBackend::startRecording(CallId, const std::string &) { return false; }
bool FakeSipBackend::stopRecording(CallId) { return false; }
bool FakeSipBackend::playFile(CallId, const std::string &, bool) { return false; }
bool FakeSipBackend::stopFile(CallId) { return false; }
StreamStats FakeSipBackend::streamStats(CallId) const { return {}; }
bool FakeSipBackend::isMediaActive(CallId) const { return false; }
bool FakeSipBackend::isCaptureTransmitting(CallId) const { return false; }
void FakeSipBackend::releaseCall(CallId) {}
CallId FakeSipBackend::simulateIncomingCall(AccountId, const std::string &, const std::string &) { return kInvalidCallId; }
void FakeSipBackend::simulateRemoteAnswer(CallId) {}
void FakeSipBackend::simulateRemoteHangup(CallId, int) {}
void FakeSipBackend::simulateTransferStatus(CallId, int, bool, const std::string &) {}
FakeSipBackend::FakeCall *FakeSipBackend::liveCall(CallId) { return nullptr; }

} // namespace compactphone::sipbackend

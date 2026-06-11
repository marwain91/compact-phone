#include "PjsipBackend.h"

#include "core/SipEngine.h"

#include <spdlog/spdlog.h>

#include <type_traits>

namespace compactphone::sipbackend {

// Guard: every ISipBackend pure virtual must be overridden — the class
// must be concrete. A static_assert catches a missed override before any
// test tries to instantiate this class.
static_assert(!std::is_abstract_v<PjsipBackend>,
              "PjsipBackend is still abstract — a pure virtual is missing");

// PjsipAccount is a pj::Account subclass that lives in Task 4. Declared
// here as an opaque private class so the header compiles without pj headers.
// The unique_ptr<PjsipAccount> in the map requires a complete type at
// destructor time; the empty definition satisfies that.
class PjsipBackend::PjsipAccount {
public:
    virtual ~PjsipAccount() = default;
};

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

PjsipBackend::PjsipBackend(sip::SipEngine *engine)
    : m_engine(engine)
{
}

PjsipBackend::~PjsipBackend() = default;

// ---------------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------------

void PjsipBackend::setListener(ISipBackendListener *listener)
{
    // Quiesce barrier: invalidate first so lambdas queued for the previous
    // listener no-op when they run (contract rule 4: no event delivered after
    // setListener(nullptr)), then assign the new listener.
    m_events.invalidate();
    m_listener = listener;
}

bool PjsipBackend::start(const EngineConfig &cfg)
{
    // NOTE (phase-2 dual-ownership): PhoneController may have already started
    // SipEngine directly. SipEngine::start() no-ops returning true when already
    // running, so cfg.sipPort is NOT applied to the live endpoint in that case.
    return m_engine->start(cfg.sipPort);
}

void PjsipBackend::stop()
{
    // Drop all accounts first (future Task 4: calls setRegistration(false)).
    m_accounts.clear();
    // Clear the native-call-id bridge map so a restarted backend starts empty
    // (contract rule). The id counters stay monotonic by design — they are
    // never reset so stale numeric ids from before the stop cannot alias new ones.
    m_nativeCallIds.clear();
    // Invalidate the event queue so nothing queued before this fires.
    m_events.invalidate();
    m_engine->stop();
}

bool PjsipBackend::isRunning() const
{
    return m_engine->isRunning();
}

// ---------------------------------------------------------------------------
// Engine-level config — thin delegation to SipEngine
// ---------------------------------------------------------------------------

void PjsipBackend::setCaTrust(const CaTrust &trust)
{
    // Store the CA file path. In-memory PEM resolution (Windows ROOT store)
    // happens inside SipEngine::start() — it reads m_caCertBuf there, and
    // that logic does NOT move here. We therefore only forward the file path.
    // trust.caPem is deliberately not forwarded: SipEngine resolves the
    // Windows ROOT store internally and owns the in-memory PEM path.
    //
    // Ordering note: this value only affects transports created afterwards.
    // Call setCaTrust() before start() so the shared TLS transport picks it
    // up; per-account transports read the setting at registration time.
    m_engine->setCaCertFile(trust.caFile);
}

void PjsipBackend::setStunServers(const std::vector<std::string> &servers)
{
    m_engine->applyStunServers(servers);
}

void PjsipBackend::setCodecPriority(const std::vector<std::string> &priorityOrder)
{
    m_engine->applyCodecPriority(priorityOrder);
}

std::vector<AudioDevice> PjsipBackend::audioDevices() const
{
    std::vector<AudioDevice> result;
    for (const auto &dev : m_engine->audioDevices()) {
        result.push_back({dev.id, dev.name, dev.inputCount, dev.outputCount});
    }
    return result;
}

int PjsipBackend::captureDevice() const
{
    return m_engine->captureDevice();
}

int PjsipBackend::playbackDevice() const
{
    return m_engine->playbackDevice();
}

bool PjsipBackend::setCaptureDevice(int id)
{
    return m_engine->setCaptureDevice(id);
}

bool PjsipBackend::setPlaybackDevice(int id)
{
    return m_engine->setPlaybackDevice(id);
}

void PjsipBackend::refreshAudioDevices()
{
    m_engine->refreshAudioDevices();
}

// ---------------------------------------------------------------------------
// Phase 5 stubs — ringtone and log sink
// ---------------------------------------------------------------------------

bool PjsipBackend::playRingtone(const std::string & /*path*/)
{
    // phase 5 stub: RingtonePlayer integration not yet wired through the adapter
    return false;
}

void PjsipBackend::stopRingtone()
{
    // phase 5 stub
}

void PjsipBackend::setLogSink(std::function<void(int, const std::string &)> /*sink*/)
{
    // phase 5 stub: SipLog wiring through the adapter is deferred
}

// ---------------------------------------------------------------------------
// Accounts — Task 4 (markers only for now)
// ---------------------------------------------------------------------------

AccountId PjsipBackend::addAccount(const AccountSettings & /*settings*/)
{
    // Task 4: translate AccountSettings → pj::AccountConfig and register
    return kInvalidAccountId;
}

bool PjsipBackend::removeAccount(AccountId /*id*/)
{
    // Task 4
    return false;
}

bool PjsipBackend::sendMessage(AccountId /*id*/, const std::string & /*toUri*/,
                               const std::string & /*body*/)
{
    // Task 4
    return false;
}

// ---------------------------------------------------------------------------
// Presence — phase 5 stubs
// ---------------------------------------------------------------------------

WatchId PjsipBackend::watch(AccountId /*accountId*/, const std::string & /*uri*/)
{
    // phase 5 stub
    return kInvalidWatchId;
}

bool PjsipBackend::unwatch(WatchId /*id*/)
{
    // phase 5 stub
    return false;
}

// ---------------------------------------------------------------------------
// Calls — phase 3 stubs
// All call methods return failure/no-op until the calls path lands.
// ---------------------------------------------------------------------------

CallId PjsipBackend::makeCall(AccountId /*accountId*/, const std::string & /*uri*/)
{
    // phase 3 stub
    return kInvalidCallId;
}

bool PjsipBackend::answer(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::decline(CallId /*id*/, int /*sipCode*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::redirect(CallId /*id*/, const std::string & /*contactUri*/)
{
    // phase 3 stub
    return false;
}

void PjsipBackend::hangup(CallId /*id*/)
{
    // phase 3 stub
}

bool PjsipBackend::hold(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::unhold(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::setMuted(CallId /*id*/, bool /*muted*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::sendDtmf(CallId /*id*/, const std::string & /*digits*/,
                             DtmfMethod /*method*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::blindTransfer(CallId /*id*/, const std::string & /*targetUri*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::attendedTransfer(CallId /*id*/, CallId /*otherId*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::bridge(CallId /*id*/, CallId /*otherId*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::startRecording(CallId /*id*/, const std::string & /*path*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::stopRecording(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::playFile(CallId /*id*/, const std::string & /*path*/, bool /*loop*/)
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::stopFile(CallId /*id*/)
{
    // phase 3 stub
    return false;
}

StreamStats PjsipBackend::streamStats(CallId /*id*/) const
{
    // phase 3 stub: returns an unpopulated stats struct (-1 = not available)
    return {};
}

bool PjsipBackend::isMediaActive(CallId /*id*/) const
{
    // phase 3 stub
    return false;
}

bool PjsipBackend::isCaptureTransmitting(CallId /*id*/) const
{
    // phase 3 stub
    return false;
}

void PjsipBackend::releaseCall(CallId /*id*/)
{
    // phase 3 stub
}

// ---------------------------------------------------------------------------
// Transitional bridges — removed in phase 3
// ---------------------------------------------------------------------------

pj::Account *PjsipBackend::pjAccountFor(AccountId /*id*/)
{
    // Task 4: return the PjsipAccount's pj::Account base for the given id
    return nullptr;
}

int PjsipBackend::nativeCallIdFor(CallId id) const
{
    // Task 4: look up the pjsua native call id minted in announceIncomingCall
    const auto it = m_nativeCallIds.find(id);
    return it != m_nativeCallIds.end() ? it->second : -1;
}

} // namespace compactphone::sipbackend

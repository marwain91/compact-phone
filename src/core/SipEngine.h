#pragma once

#include <memory>
#include <string>
#include <vector>

namespace pj { class Endpoint; }

namespace compactphone::sip {

// Owns the pj::Endpoint. Single instance per process.
// Methods are NOT thread-safe; call from the main thread only.
class SipEngine {
public:
    SipEngine();
    ~SipEngine();

    SipEngine(const SipEngine &) = delete;
    SipEngine &operator=(const SipEngine &) = delete;

    bool start(int sipPort);
    void stop();

    bool isRunning() const { return m_running; }
    pj::Endpoint *endpoint();

    // Apply the union of STUN servers gathered from all accounts. Called at
    // startup and whenever accounts change. PJSUA holds STUN config at the
    // endpoint level (not per-account), so we aggregate and re-init.
    void applyStunServers(const std::vector<std::string> &servers);

    // Push a codec priority order to PJSIP. Names like "opus", "PCMU",
    // "PCMA", "G722"; later codecs in the list get lower priority. Codecs
    // not mentioned keep their default priority.
    void applyCodecPriority(const std::vector<std::string> &priorityOrder);

private:
    bool m_running = false;
    std::unique_ptr<pj::Endpoint> m_endpoint;
};

} // namespace compactphone::sip

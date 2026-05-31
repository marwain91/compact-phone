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

    // --- TLS CA trust ---
    // PEM file of trusted CA certificates used to verify SIP-over-TLS server
    // certificates. Must be set BEFORE start() to take effect (transports are
    // created there). If left unset, start() auto-detects it from the
    // COMPACTPHONE_CA_FILE env var, then common system bundle locations. When
    // empty, a verifying TLS transport has no trust anchors and rejects every
    // certificate, so configuring this is required for default (verify-on)
    // TLS accounts to reach a legitimately-signed server.
    void setCaCertFile(const std::string &path) { m_caCertFile = path; }
    const std::string &caCertFile() const { return m_caCertFile; }

    // --- Audio device management ---
    // Owns all pjsua2 AudDevManager access so higher layers (e.g.
    // SettingsController) don't reach into the endpoint directly. All methods
    // are safe to call when the engine is stopped (return empty / -1 / false).
    struct AudioDevice {
        int id = -1;
        std::string name;
        int inputCount = 0;
        int outputCount = 0;
    };
    std::vector<AudioDevice> audioDevices() const;
    int captureDevice() const;
    int playbackDevice() const;
    bool setCaptureDevice(int id);
    bool setPlaybackDevice(int id);
    void refreshAudioDevices();

private:
    bool m_running = false;
    std::unique_ptr<pj::Endpoint> m_endpoint;
    std::string m_caCertFile;
};

} // namespace compactphone::sip

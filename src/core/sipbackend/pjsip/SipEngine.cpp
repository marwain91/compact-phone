#include "SipEngine.h"
#include "SipLog.h"

#include <pjsua2.hpp>
#include <pjsua-lib/pjsua.h>
#include <spdlog/spdlog.h>

#include <QCoreApplication>
#include <QFileInfo>

#include <cctype>
#include <cstdlib>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <wincrypt.h>
#endif

namespace compactphone::sip {

namespace {

// Locate a PEM bundle of trusted CA certificates: an explicit override via the
// COMPACTPHONE_CA_FILE env var first, then common system locations. Empty if
// none found (caller must then accept that verifying TLS has no trust anchors).
std::string detectCaCertFile()
{
    if (const char *env = std::getenv("COMPACTPHONE_CA_FILE"); env && *env) {
        return env;
    }
    static const char *const kCandidates[] = {
        "/etc/ssl/certs/ca-certificates.crt", // Debian/Ubuntu/Alpine
        "/etc/pki/tls/certs/ca-bundle.crt",   // RHEL/Fedora
        "/etc/ssl/cert.pem",                  // macOS, Alpine, *BSD
    };
    for (const char *path : kCandidates) {
        if (QFileInfo::exists(QString::fromLatin1(path))) return path;
    }
    return {};
}

#ifdef _WIN32
// Windows ships no on-disk PEM CA bundle the way Linux/macOS do — trust lives
// in the system certificate store. Enumerate the machine ROOT store (the
// trusted-root CA store, kept current by Windows Update) and serialise each
// cert to a concatenated PEM buffer that OpenSSL (PJSIP's TLS backend) can
// parse via CaBuf. Without this, every default verify-on TLS account on
// Windows rejects all server certs for lack of any trust anchor.
std::string loadWindowsRootStorePem()
{
    HCERTSTORE store = CertOpenSystemStoreA(0, "ROOT");
    if (!store) {
        spdlog::warn("SipEngine: cannot open Windows ROOT cert store (err {})",
                     GetLastError());
        return {};
    }

    std::string pem;
    int count = 0;
    PCCERT_CONTEXT ctx = nullptr;
    while ((ctx = CertEnumCertificatesInStore(store, ctx)) != nullptr) {
        DWORD len = 0;
        if (!CryptBinaryToStringA(ctx->pbCertEncoded, ctx->cbCertEncoded,
                                  CRYPT_STRING_BASE64HEADER, nullptr, &len)
            || len == 0) {
            continue; // skip a cert we can't encode rather than abort the lot
        }
        std::string block(len, '\0');
        if (!CryptBinaryToStringA(ctx->pbCertEncoded, ctx->cbCertEncoded,
                                  CRYPT_STRING_BASE64HEADER, block.data(),
                                  &len)) {
            continue;
        }
        block.resize(len); // drop the trailing NUL the API counts on the way in
        pem += block;      // BASE64HEADER blocks already carry BEGIN/END lines
        ++count;
    }
    CertCloseStore(store, 0);

    spdlog::info("SipEngine: loaded {} CA certs from the Windows ROOT store",
                 count);
    return pem;
}
#endif // _WIN32

} // namespace

SipEngine::SipEngine() = default;

SipEngine::~SipEngine()
{
    if (m_running) stop();
}

void SipEngine::applyCaTrust(pj::TlsConfig &cfg) const
{
    // CaListFile wins over CaBuf in PJSIP, so set exactly one: the file when
    // we have it, otherwise the in-memory bundle.
    if (!m_caCertFile.empty()) {
        cfg.CaListFile = m_caCertFile;
    } else if (!m_caCertBuf.empty()) {
        cfg.CaBuf = m_caCertBuf;
    }
}

bool SipEngine::start(int sipPort)
{
    if (m_running) return true;

    m_endpoint = std::make_unique<pj::Endpoint>();

    // Resolve the CA trust anchors once (unless a caller set a file
    // explicitly). Verifying TLS transports need anchors or they reject every
    // cert. Prefer an on-disk PEM bundle (env override / system bundle); on
    // Windows, which has no such file, fall back to the OS ROOT cert store
    // loaded into an in-memory PEM buffer.
    if (m_caCertFile.empty()) m_caCertFile = detectCaCertFile();
#ifdef _WIN32
    if (m_caCertFile.empty() && m_caCertBuf.empty()) {
        m_caCertBuf = loadWindowsRootStorePem();
    }
#endif
    if (!m_caCertFile.empty()) {
        spdlog::info("SipEngine: TLS CA bundle: {}", m_caCertFile);
    } else if (!m_caCertBuf.empty()) {
        spdlog::info("SipEngine: TLS CA trust from in-memory bundle "
                     "({} bytes)", m_caCertBuf.size());
    } else {
        spdlog::warn("SipEngine: no CA trust anchors found; default (verify-on) "
                     "TLS accounts will reject all server certs. Set "
                     "COMPACTPHONE_CA_FILE.");
    }

    try {
        m_endpoint->libCreate();

        pj::EpConfig epCfg;
        epCfg.uaConfig.userAgent = "CompactPhone/0.1";
        epCfg.uaConfig.maxCalls = PJSUA_MAX_CALLS > 1
            ? PJSUA_MAX_CALLS - 1
            : PJSUA_MAX_CALLS;
        epCfg.logConfig.level = 4;
        epCfg.logConfig.consoleLevel = 4;
        epCfg.logConfig.writer = spdlogPjsipWriter();
        m_endpoint->libInit(epCfg);

        pj::TransportConfig udpCfg;
        udpCfg.port = static_cast<unsigned>(sipPort);
        m_endpoint->transportCreate(PJSIP_TRANSPORT_UDP, udpCfg);

        // TCP transport for transport=tcp accounts. Also used as the
        // fallback when a UDP request exceeds 1300 bytes (RFC 3261
        // §18.1.1) — without this, PJSIP would silently drop oversized
        // REGISTER/INVITE attempts on TCP-only PBXes.
        try {
            pj::TransportConfig tcpCfg;
            tcpCfg.port = 0;
            m_endpoint->transportCreate(PJSIP_TRANSPORT_TCP, tcpCfg);
        } catch (const pj::Error &e) {
            spdlog::warn("SipEngine: TCP transport create failed: {}", e.info());
        }

        // TLS transport for sips:/transport=tls accounts. Each TLS account
        // gets its OWN transport carrying its own verify policy, bound via
        // AccountConfig::sipConfig::transportId (see
        // AccountsManager::registerAccount). This shared transport is only a
        // fallback for any TLS connection not bound to a per-account
        // transport, so it MUST fail closed: verify the server certificate by
        // default. Accounts that explicitly opt into allowUntrustedCert get a
        // permissive per-account transport instead — a misconfiguration or a
        // MITM can never silently downgrade a default account to no-verify.
        pj::TransportConfig tlsCfg;
        tlsCfg.port = 0;
        tlsCfg.tlsConfig.method = PJSIP_TLSV1_2_METHOD;
        tlsCfg.tlsConfig.verifyServer = true;
        tlsCfg.tlsConfig.verifyClient = false;
        applyCaTrust(tlsCfg.tlsConfig);
        try {
            m_endpoint->transportCreate(PJSIP_TRANSPORT_TLS, tlsCfg);
        } catch (const pj::Error &e) {
            spdlog::warn("SipEngine: TLS transport create failed (UDP-only OK): {}",
                         e.info());
        }

        m_endpoint->libStart();

        // Probe for a real audio device. If none exists (headless container,
        // CI), fall back to the null audio device so PJSIP can still place
        // calls without crashing on the missing default device. On real
        // desktops PJSIP picks the OS default; we leave it alone.
        try {
            auto &mgr = m_endpoint->audDevManager();
            auto *app = QCoreApplication::instance();
            const bool headless = !app || !app->inherits("QGuiApplication");
            if (headless) {
                spdlog::info("SipEngine: headless process, using null audio dev");
                mgr.setNullDev();
            } else {
                const auto devs = mgr.enumDev2();
                bool hasReal = false;
                for (const auto &d : devs) {
                    if (d.inputCount > 0 || d.outputCount > 0) {
                        hasReal = true;
                        break;
                    }
                }
                if (hasReal) {
                    spdlog::info("SipEngine: using OS default audio device");
                } else {
                    spdlog::info("SipEngine: no real audio device, using null dev");
                    mgr.setNullDev();
                }
            }
        } catch (const pj::Error &e) {
            spdlog::warn("SipEngine: audio dev probe failed: {}", e.info());
        }

        m_running = true;
        spdlog::info("SipEngine started, UDP port={}", sipPort);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("SipEngine::start failed: {} (status {})", e.info(), e.status);
        m_endpoint.reset();
        return false;
    }
}

void SipEngine::stop()
{
    if (!m_running) return;
    try {
        m_endpoint->hangupAllCalls();
        m_endpoint->libDestroy();
    } catch (const pj::Error &e) {
        spdlog::error("SipEngine::stop error: {}", e.info());
    }
    m_endpoint.reset();
    m_running = false;
    spdlog::info("SipEngine stopped");
}

pj::Endpoint *SipEngine::endpoint() { return m_endpoint.get(); }

void SipEngine::applyStunServers(const std::vector<std::string> &servers)
{
    if (!m_endpoint) return;
    // PJSUA2 doesn't expose runtime UA-config mutation, but the underlying
    // C API has pjsua_update_stun_servers() which re-resolves on the next
    // registration. Build pj_str_t views into a stable storage vector so
    // pointers stay valid until the call returns.
    std::vector<std::string> storage;
    storage.reserve(servers.size());
    for (const auto &s : servers) {
        if (!s.empty()) storage.push_back(s);
    }
    if (storage.empty()) {
        spdlog::info("SipEngine: no STUN servers configured");
        return;   // pjsua_update_stun_servers asserts on count==0
    }
    std::vector<pj_str_t> ptrs;
    ptrs.reserve(storage.size());
    for (auto &s : storage) {
        pj_str_t v;
        v.ptr = s.data();
        v.slen = static_cast<pj_ssize_t>(s.size());
        ptrs.push_back(v);
    }
    const pj_status_t st = pjsua_update_stun_servers(
        static_cast<unsigned>(ptrs.size()), ptrs.data(), PJ_FALSE);
    if (st != PJ_SUCCESS) {
        spdlog::warn("SipEngine::applyStunServers: pjsua_update_stun_servers={}", st);
    } else {
        spdlog::info("SipEngine: applied {} STUN server(s)", ptrs.size());
    }
}

void SipEngine::applyCodecPriority(const std::vector<std::string> &priorityOrder)
{
    if (!m_endpoint) return;
    if (priorityOrder.empty()) return;
    try {
        // Lower the priority of every audio codec first, then promote the
        // requested ones in order. PJSIP default priority is ~130; we set
        // others to 0 (still enabled, but not preferred).
        const auto all = m_endpoint->codecEnum2();
        for (const auto &c : all) {
            try { m_endpoint->codecSetPriority(c.codecId, 0); } catch (...) {}
        }
        pj_uint8_t prio = 254;
        for (const auto &name : priorityOrder) {
            for (const auto &c : all) {
                // codecId is like "opus/48000/2", "PCMU/8000/1" — match the
                // user-typed name as a case-insensitive prefix.
                if (c.codecId.size() < name.size()) continue;
                bool match = true;
                for (size_t i = 0; i < name.size(); ++i) {
                    char a = std::tolower(static_cast<unsigned char>(c.codecId[i]));
                    char b = std::tolower(static_cast<unsigned char>(name[i]));
                    if (a != b) { match = false; break; }
                }
                if (match) {
                    try { m_endpoint->codecSetPriority(c.codecId, prio); } catch (...) {}
                    spdlog::info("SipEngine: codec {} priority={}", c.codecId, prio);
                    if (prio >= 2) prio -= 2;
                    break;
                }
            }
        }
    } catch (const pj::Error &e) {
        spdlog::warn("SipEngine::applyCodecPriority: {}", e.info());
    }
}

std::vector<SipEngine::AudioDevice> SipEngine::audioDevices() const
{
    std::vector<AudioDevice> out;
    if (!m_endpoint) return out;
    try {
        const auto devs = m_endpoint->audDevManager().enumDev2();
        for (size_t i = 0; i < devs.size(); ++i) {
            AudioDevice d;
            // Use the pjmedia device id, NOT the position in the enum vector.
            // setCaptureDev()/setPlaybackDev()/getCaptureDev() all speak device
            // ids, and pjsua2 does not guarantee id == index — on macOS
            // CoreAudio they diverge, so indexing by position selects the wrong
            // device (or none) and the picked device never takes effect.
            d.id = devs[i].id;
            d.name = devs[i].name;
            d.inputCount = devs[i].inputCount;
            d.outputCount = devs[i].outputCount;
            spdlog::debug("SipEngine: audio dev id={} \"{}\" in={} out={}",
                          d.id, d.name, d.inputCount, d.outputCount);
            out.push_back(std::move(d));
        }
    } catch (const pj::Error &e) {
        spdlog::warn("SipEngine::audioDevices: {}", e.info());
    }
    return out;
}

int SipEngine::captureDevice() const
{
    if (!m_endpoint) return -1;
    try { return m_endpoint->audDevManager().getCaptureDev(); }
    catch (const pj::Error &e) {
        spdlog::warn("SipEngine::captureDevice: {}", e.info());
        return -1;
    }
}

int SipEngine::playbackDevice() const
{
    if (!m_endpoint) return -1;
    try { return m_endpoint->audDevManager().getPlaybackDev(); }
    catch (const pj::Error &e) {
        spdlog::warn("SipEngine::playbackDevice: {}", e.info());
        return -1;
    }
}

bool SipEngine::setCaptureDevice(int id)
{
    if (!m_endpoint) return false;
    try { m_endpoint->audDevManager().setCaptureDev(id); return true; }
    catch (const pj::Error &e) {
        spdlog::warn("SipEngine::setCaptureDevice: {}", e.info());
        return false;
    }
}

bool SipEngine::setPlaybackDevice(int id)
{
    if (!m_endpoint) return false;
    try { m_endpoint->audDevManager().setPlaybackDev(id); return true; }
    catch (const pj::Error &e) {
        spdlog::warn("SipEngine::setPlaybackDevice: {}", e.info());
        return false;
    }
}

void SipEngine::refreshAudioDevices()
{
    if (!m_endpoint) return;
    try { m_endpoint->audDevManager().refreshDevs(); }
    catch (const pj::Error &e) {
        spdlog::warn("SipEngine::refreshAudioDevices: {}", e.info());
    }
}

} // namespace compactphone::sip

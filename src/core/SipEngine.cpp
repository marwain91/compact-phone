#include "SipEngine.h"
#include "SipLog.h"

#include <pjsua2.hpp>
#include <pjsua-lib/pjsua.h>
#include <spdlog/spdlog.h>

#include <QCoreApplication>

#include <cctype>
#include <vector>

namespace compactphone::sip {

SipEngine::SipEngine() = default;

SipEngine::~SipEngine()
{
    if (m_running) stop();
}

bool SipEngine::start(int sipPort)
{
    if (m_running) return true;

    m_endpoint = std::make_unique<pj::Endpoint>();

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

        // TLS transport for sips:/transport=tls accounts. Per-account TLS
        // verification policy is enforced on pj::AccountConfig::sipConfig.tlsConfig;
        // the transport-level tlsConfig here is the unbound default.
        pj::TransportConfig tlsCfg;
        tlsCfg.port = 0;
        tlsCfg.tlsConfig.method = PJSIP_TLSV1_2_METHOD;
        // v0.2c limitation: TLS verify is transport-level in PJSUA2, not
        // per-account. We default to permissive here so self-signed test
        // PBXes work; per-account allowUntrustedCert becomes functional in
        // v0.3 via per-account transport allocation.
        tlsCfg.tlsConfig.verifyServer = false;
        tlsCfg.tlsConfig.verifyClient = false;
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

} // namespace compactphone::sip

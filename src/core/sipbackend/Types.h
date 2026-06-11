#pragma once

// Stack-neutral value types shared by every SIP backend (PJSIP adapter,
// baresip adapter, test fake). This header must stay free of pj::, Qt,
// and core/ includes — it IS the boundary. See
// docs/superpowers/specs/2026-06-11-sip-backend-abstraction-design.md.

#include <cstdint>
#include <string>

namespace compactphone::sipbackend {

// Backend-minted opaque ids. Valid from the returning call / delivering
// event until releaseCall()/removeAccount()/unwatch().
using AccountId = std::int32_t;
using CallId = std::int32_t;
using WatchId = std::int32_t;
constexpr AccountId kInvalidAccountId = -1;
constexpr CallId kInvalidCallId = -1;
constexpr WatchId kInvalidWatchId = -1;

// Mirrors sip::CallState value-for-value (CallManager migrates onto this
// enum in phase 4).
enum class CallState {
    Idle,
    Calling,
    EarlyMedia,
    Confirmed,
    Disconnected,
};

// Deliberately redefined here rather than including core/Account.h: the
// boundary header owns its vocabulary. AccountsManager maps sip:: enums
// to these at the seam (phase 2).
enum class Transport { Udp, Tcp, Tls };
enum class SrtpMode { Disabled, Optional, Required };
enum class DtmfMethod { Inband, Rfc2833, Info };

// Mirrors LineState in WatchedLine.h (Unknown/Idle/Busy/Offline).
enum class PresenceState { Unknown, Idle, Busy, Offline };

struct EngineConfig {
    int sipPort = 5060;
};

// Trust anchors for verify-on TLS. Platform resolution (env var, system
// bundles, Windows ROOT store) stays in core; backends receive the
// resolved result. Exactly one of the two fields is non-empty.
struct CaTrust {
    std::string caFile;  // path to a PEM bundle on disk
    std::string caPem;   // in-memory PEM bundle (Windows ROOT store)
};

struct AudioDevice {
    int id = -1;
    std::string name;
    int inputCount = 0;
    int outputCount = 0;
};

// Same semantics as CallManager::StreamStats today: -1 = not populated.
struct StreamStats {
    double mos = -1.0;
    double lossPct = -1.0;
    int rttMs = -1;
    int jitterMs = -1;
};

// Stack-neutral registration request. Built by AccountsManager from the
// Account value object plus the keychain-resolved password (phase 2).
// UI-only fields (label, provider, sortOrder, isDefault, enabled) and
// engine-level fields (stunServer, codecs) stay out: STUN and codec
// priority are aggregated engine-wide, exactly as SipEngine does today.
struct AccountSettings {
    std::string displayName;
    std::string username;
    std::string domain;
    std::string authUser;             // defaults to username if empty
    std::string authRealm;            // defaults to "*" when empty
    std::string password;
    Transport transport = Transport::Udp;
    std::string proxy;
    std::string publicAddress;        // overrides STUN-discovered IP
    int registerIntervalSec = 0;      // 0 = backend default
    int keepaliveIntervalSec = 0;     // 0 = backend default
    bool sessionTimersEnabled = true;
    bool publishPresenceEnabled = false;
    bool iceEnabled = false;
    bool hideCallerId = false;
    bool zrtpEnabled = false;
    SrtpMode srtpMode = SrtpMode::Optional;
    bool allowUntrustedCert = false;
    DtmfMethod dtmfMethod = DtmfMethod::Rfc2833;
};

} // namespace compactphone::sipbackend

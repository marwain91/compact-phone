#pragma once

#include <cstdint>
#include <string>

namespace compactphone::sip {

using AccountId = std::int32_t;
constexpr AccountId kInvalidAccountId = -1;

enum class Transport { Udp, Tcp, Tls };
enum class SrtpMode { Disabled, Optional, Required };
enum class DtmfMethod { Inband, Rfc2833, Info };

// Plain value object — owns no resources, copyable, no PJSIP coupling.
// AccountsManager translates this into pj::AccountConfig at registration time.
struct Account {
    AccountId id = kInvalidAccountId;
    std::string label;          // human-readable, UI only (e.g. "Work Office")
    std::string displayName;    // SIP From: display name
    std::string username;
    std::string domain;
    std::string authUser;       // defaults to username if empty
    std::string passwordRef;    // opaque keychain reference
    Transport transport = Transport::Udp;
    std::string proxy;
    std::string stunServer;
    std::string publicAddress;          // overrides STUN-discovered external IP
    std::string codecs;                 // comma-list, priority order, "" = pjsip default
    std::string voicemailNumber;        // dial this to access voicemail
    bool registerOnStartup = true;
    int registerIntervalSec = 0;        // 0 = pjsip default (300)
    int keepaliveIntervalSec = 0;       // 0 = pjsip default
    bool sessionTimersEnabled = true;
    bool publishPresenceEnabled = false;
    bool iceEnabled = false;
    bool hideCallerId = false;
    bool zrtpEnabled = false;
    SrtpMode srtpMode = SrtpMode::Optional;
    bool allowUntrustedCert = false;
    DtmfMethod dtmfMethod = DtmfMethod::Rfc2833;
    bool isDefault = false;
    bool enabled = true;
    int sortOrder = 0;
};

// String<->enum helpers for SQLite persistence.
const char *transportToString(Transport t);
Transport transportFromString(const std::string &s);

const char *srtpModeToString(SrtpMode m);
SrtpMode srtpModeFromString(const std::string &s);

const char *dtmfMethodToString(DtmfMethod m);
DtmfMethod dtmfMethodFromString(const std::string &s);

} // namespace compactphone::sip

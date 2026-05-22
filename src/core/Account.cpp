#include "Account.h"

namespace compactphone::sip {

const char *transportToString(Transport t)
{
    switch (t) {
    case Transport::Udp: return "udp";
    case Transport::Tcp: return "tcp";
    case Transport::Tls: return "tls";
    }
    return "udp";
}

Transport transportFromString(const std::string &s)
{
    if (s == "tcp") return Transport::Tcp;
    if (s == "tls") return Transport::Tls;
    return Transport::Udp;
}

const char *srtpModeToString(SrtpMode m)
{
    switch (m) {
    case SrtpMode::Disabled: return "disabled";
    case SrtpMode::Optional: return "optional";
    case SrtpMode::Required: return "required";
    }
    return "optional";
}

SrtpMode srtpModeFromString(const std::string &s)
{
    if (s == "disabled") return SrtpMode::Disabled;
    if (s == "required" || s == "mandatory") return SrtpMode::Required;
    return SrtpMode::Optional;
}

const char *dtmfMethodToString(DtmfMethod m)
{
    switch (m) {
    case DtmfMethod::Inband: return "inband";
    case DtmfMethod::Rfc2833: return "rfc2833";
    case DtmfMethod::Info: return "info";
    }
    return "rfc2833";
}

DtmfMethod dtmfMethodFromString(const std::string &s)
{
    if (s == "inband") return DtmfMethod::Inband;
    if (s == "info") return DtmfMethod::Info;
    return DtmfMethod::Rfc2833;
}

} // namespace compactphone::sip

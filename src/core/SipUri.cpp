#include "SipUri.h"

#include <algorithm>
#include <cctype>

namespace compactphone::sip {

namespace {

std::string trim(std::string s)
{
    auto isSpace = [](unsigned char c) { return std::isspace(c) != 0; };
    s.erase(s.begin(), std::find_if_not(s.begin(), s.end(), isSpace));
    s.erase(std::find_if_not(s.rbegin(), s.rend(), isSpace).base(), s.end());
    return s;
}

bool ieq(const std::string &s, size_t pos, const char *needle)
{
    for (size_t i = 0; needle[i]; ++i) {
        if (pos + i >= s.size()) return false;
        if (std::tolower(static_cast<unsigned char>(s[pos + i]))
            != needle[i]) return false;
    }
    return true;
}

bool startsWithScheme(const std::string &s)
{
    return ieq(s, 0, "sip:") || ieq(s, 0, "sips:");
}

const char *schemeFor(Transport transport)
{
    return transport == Transport::Tls ? "sips:" : "sip:";
}

std::string toLower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return s;
}

// Remove injection vectors from an externally-supplied dial target before it
// reaches PJSIP's URI parser / INVITE construction:
//   - control characters (CR/LF/tab/etc.) — never legitimate in a dial target
//     and the classic SIP header-injection vector;
//   - embedded URI headers ("?Route=...&Call-Info=...") — a dialed target must
//     not be able to rewrite INVITE headers / force routing;
//   - URI parameters other than an allowlist (transport, user) — drops routing
//     params like maddr/lr/ttl/method that could redirect the call.
// Legitimate user@host[:port] and transport/user params are preserved.
std::string sanitizeTarget(std::string s)
{
    s.erase(std::remove_if(s.begin(), s.end(),
                           [](unsigned char c) { return c < 0x20 || c == 0x7f; }),
            s.end());
    if (const auto q = s.find('?'); q != std::string::npos) s.erase(q);

    const auto semi = s.find(';');
    if (semi == std::string::npos) return s;
    std::string out = s.substr(0, semi);
    const std::string params = s.substr(semi + 1);
    size_t start = 0;
    while (start <= params.size()) {
        const auto end = params.find(';', start);
        const std::string param =
            params.substr(start, (end == std::string::npos ? params.size() : end) - start);
        if (!param.empty()) {
            const auto eq = param.find('=');
            const std::string name =
                toLower(eq == std::string::npos ? param : param.substr(0, eq));
            if (name == "transport" || name == "user") {
                out += ';';
                out += param;
            }
        }
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return out;
}

} // namespace

std::string normalizeSipTarget(const std::string &target,
                               const std::string &domain,
                               Transport transport)
{
    const auto cleanTarget = sanitizeTarget(trim(target));
    if (cleanTarget.empty()) return {};
    if (startsWithScheme(cleanTarget)) return cleanTarget;

    std::string uri = schemeFor(transport);
    uri += cleanTarget;
    if (cleanTarget.find('@') == std::string::npos && !domain.empty()) {
        uri += '@';
        uri += domain;
    }
    return uri;
}

} // namespace compactphone::sip

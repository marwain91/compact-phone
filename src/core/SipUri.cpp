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

} // namespace

std::string normalizeSipTarget(const std::string &target,
                               const std::string &domain,
                               Transport transport)
{
    const auto cleanTarget = trim(target);
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

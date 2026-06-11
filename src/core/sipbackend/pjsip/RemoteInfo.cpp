#include "RemoteInfo.h"

namespace compactphone::sipbackend {

RemoteInfo parseRemoteInfo(const std::string &raw)
{
    if (raw.empty())
        return {};

    const auto lt = raw.find('<');
    const auto gt = raw.find('>');

    if (lt != std::string::npos && gt != std::string::npos && gt > lt) {
        RemoteInfo ri;
        ri.uri = raw.substr(lt + 1, gt - lt - 1);

        // Display name is the text before '<'; strip trailing whitespace
        // then strip enclosing double-quotes.
        std::string dn = raw.substr(0, lt);
        while (!dn.empty() && (dn.back() == ' ' || dn.back() == '\t'))
            dn.pop_back();
        if (dn.size() >= 2 && dn.front() == '"' && dn.back() == '"')
            dn = dn.substr(1, dn.size() - 2);
        ri.displayName = dn;
        return ri;
    }

    // No angle brackets — the whole string is the URI.
    return RemoteInfo{raw, {}};
}

} // namespace compactphone::sipbackend

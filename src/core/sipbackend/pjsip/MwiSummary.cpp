#include "MwiSummary.h"

#include <cstring>
#include <cstdio>

namespace compactphone::sipbackend {

// Parsing logic moved verbatim (bug-for-bug) from AccountImpl::onMwiInfo
// in AccountsManager.cpp:152-169. Do NOT modify AccountsManager yet.
MwiSummary parseMwiSummary(const std::string &body)
{
    MwiSummary result;

    // Voice-Message line: "Voice-Message: N/M (urgent N/M)" — we only
    // care about the first integer pair.
    const auto pos = body.find("Voice-Message:");
    if (pos != std::string::npos) {
        const char *p = body.c_str() + pos + std::strlen("Voice-Message:");
        while (*p == ' ' || *p == '\t') ++p;
        sscanf(p, "%d/%d", &result.newMessages, &result.oldMessages);
        result.active = result.newMessages > 0;
    } else {
        const auto a = body.find("Messages-Waiting:");
        if (a != std::string::npos) {
            result.active = body.find("yes", a) != std::string::npos;
        }
    }

    return result;
}

} // namespace compactphone::sipbackend

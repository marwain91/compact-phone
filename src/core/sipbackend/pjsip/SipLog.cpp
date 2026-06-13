#include "SipLog.h"

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

#include <cstring>
#include <string>

namespace compactphone::sip {

namespace {

// Scrub SIP digest-auth secrets from a wire-log line before it reaches any
// spdlog sink (including the in-app ring buffer that exportDiagnostics writes
// to a file). The password is never on the wire under digest auth, but the
// response= hash (and cnonce=) enable an offline dictionary attack, so they
// are the sensitive tokens. PJSIP level-4 logging dumps full SIP messages, so
// this runs even though such dumps are off by default.
std::string redactCredentials(std::string s)
{
    static const char *const kKeys[] = {"response=\"", "cnonce=\""};
    for (const char *key : kKeys) {
        const size_t keyLen = std::strlen(key);
        size_t pos = 0;
        while ((pos = s.find(key, pos)) != std::string::npos) {
            const size_t valStart = pos + keyLen;
            const size_t valEnd = s.find('"', valStart);
            if (valEnd == std::string::npos) break;
            s.replace(valStart, valEnd - valStart, "<redacted>");
            pos = valStart;
        }
    }
    return s;
}

class SpdlogPjsipWriter : public pj::LogWriter {
public:
    void write(const pj::LogEntry &entry) override
    {
        const std::string msg = redactCredentials(entry.msg);
        switch (entry.level) {
        case 1: spdlog::error("[pjsip] {}", msg); break;
        case 2: spdlog::warn ("[pjsip] {}", msg); break;
        case 3: spdlog::info ("[pjsip] {}", msg); break;
        case 4: spdlog::debug("[pjsip] {}", msg); break;
        default: spdlog::trace("[pjsip] {}", msg); break;
        }
    }
};

} // namespace

// Returns a heap-allocated writer. PJSIP takes ownership of the pointer
// (LogConfig::writer is `delete`-d during libDestroy), so calling this
// with a pointer to a static or stack object causes a free()-on-static
// crash. Each successful libInit/libDestroy cycle consumes one allocation.
pj::LogWriter *spdlogPjsipWriter()
{
    return new SpdlogPjsipWriter;
}

} // namespace compactphone::sip

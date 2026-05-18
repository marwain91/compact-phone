#include "SipLog.h"

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

namespace {

class SpdlogPjsipWriter : public pj::LogWriter {
public:
    void write(const pj::LogEntry &entry) override
    {
        const auto &msg = entry.msg;
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

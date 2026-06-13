#pragma once

namespace pj { class LogWriter; }

namespace compactphone::sip {

// Returns a static spdlog-forwarding LogWriter. Assign its address to
// pj::LogConfig::writer in your EpConfig before calling Endpoint::libInit().
pj::LogWriter *spdlogPjsipWriter();

} // namespace compactphone::sip

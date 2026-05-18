#pragma once

#include "Account.h"

#include <cstdint>
#include <string>

namespace compactphone::sip {

using WatchedLineId = std::int32_t;
constexpr WatchedLineId kInvalidWatchedLineId = -1;

// One BLF/presence-watched extension. We subscribe to its presence
// event package and surface online/busy/offline to the UI.
enum class LineState {
    Unknown,    // no NOTIFY received yet
    Idle,       // online & free
    Busy,       // ringing or in-call
    Offline,    // server says they're gone
};

struct WatchedLine {
    WatchedLineId id = kInvalidWatchedLineId;
    AccountId accountId = kInvalidAccountId;
    std::string uri;
    std::string label;
    int sortOrder = 0;
    LineState state = LineState::Unknown;
};

inline const char *lineStateToString(LineState s)
{
    switch (s) {
    case LineState::Idle:    return "idle";
    case LineState::Busy:    return "busy";
    case LineState::Offline: return "offline";
    case LineState::Unknown: break;
    }
    return "unknown";
}

} // namespace compactphone::sip

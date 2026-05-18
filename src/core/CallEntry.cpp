#include "CallEntry.h"

namespace compactphone::sip {

const char *callDirectionToString(CallDirection d)
{
    return d == CallDirection::Inbound ? "in" : "out";
}

const char *callStateToCString(CallState s)
{
    switch (s) {
    case CallState::Idle:         return "idle";
    case CallState::Calling:      return "calling";
    case CallState::EarlyMedia:   return "early";
    case CallState::Confirmed:    return "active";
    case CallState::Disconnected: return "disconnected";
    }
    return "unknown";
}

} // namespace compactphone::sip

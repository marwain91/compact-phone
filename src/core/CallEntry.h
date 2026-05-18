#pragma once

#include "Account.h"
#include "CallManager.h"

#include <cstdint>
#include <string>

namespace compactphone::sip {

enum class CallDirection { Outbound, Inbound };

// Snapshot of a call for QML rendering. CallManager produces these via
// snapshot(); the values reflect the call state at the moment of the call.
struct CallEntry {
    CallId id = kInvalidCallId;
    AccountId accountId = kInvalidAccountId;
    std::string remoteUri;
    std::string remoteDisplayName;
    CallState state = CallState::Idle;
    bool held = false;
    bool muted = false;
    bool recording = false;
    CallDirection direction = CallDirection::Outbound;
};

const char *callDirectionToString(CallDirection d);
const char *callStateToCString(CallState s);

} // namespace compactphone::sip

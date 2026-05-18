#pragma once

#include "Account.h"
#include "CallEntry.h"

#include <cstdint>
#include <string>

namespace compactphone::sip {

using HistoryId = std::int32_t;
constexpr HistoryId kInvalidHistoryId = -1;

struct HistoryEntry {
    HistoryId id = kInvalidHistoryId;
    AccountId accountId = kInvalidAccountId;
    CallDirection direction = CallDirection::Outbound;
    std::string remoteUri;
    std::string remoteDisplayName;
    std::int64_t startedAt = 0;     // unix epoch ms
    std::int64_t durationMs = 0;
    std::string endReason;
};

} // namespace compactphone::sip

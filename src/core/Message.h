#pragma once

#include "Account.h"

#include <cstdint>
#include <string>

namespace compactphone::sip {

using MessageId = std::int64_t;

enum class MessageDirection { Incoming, Outgoing };

struct Message {
    MessageId id = 0;
    AccountId accountId = kInvalidAccountId;
    std::string peerUri;
    MessageDirection direction = MessageDirection::Outgoing;
    std::string body;
    std::int64_t createdAtMs = 0;
    bool read = false;
};

} // namespace compactphone::sip

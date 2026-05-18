#pragma once

#include <cstdint>
#include <string>

namespace compactphone::sip {

using ContactId = std::int32_t;
constexpr ContactId kInvalidContactId = -1;

struct Contact {
    ContactId id = kInvalidContactId;
    std::string displayName;
    std::string sipUri;
    std::string phone;
    std::string notes;
    bool favorite = false;
};

} // namespace compactphone::sip

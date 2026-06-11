#pragma once

#include <string>

namespace compactphone::sipbackend {

struct MwiSummary {
    int newMessages = 0;
    int oldMessages = 0;
    bool active = false;
};

// Parses a SIMPLE message-summary NOTIFY body: "Voice-Message: N/M"
// wins; otherwise "Messages-Waiting: yes/no" sets only `active`.
// Extracted from the former AccountImpl::onMwiInfo so the policy is
// unit-testable without a live subscription.
MwiSummary parseMwiSummary(const std::string &body);

} // namespace compactphone::sipbackend

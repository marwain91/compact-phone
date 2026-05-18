#pragma once

#include "CallEntry.h"
#include "HistoryEntry.h"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>

namespace compactphone::sip {

class CallSessionTracker {
public:
    void noteOutbound(CallId id,
                      AccountId accountId,
                      const std::string &remoteUri,
                      std::int64_t nowMs);
    void noteIncoming(const CallEntry &entry, std::int64_t nowMs);

    std::optional<HistoryEntry> noteState(const CallEntry &entry,
                                          CallState state,
                                          std::int64_t nowMs);
    void erase(CallId id);

private:
    struct Session {
        AccountId accountId = kInvalidAccountId;
        std::string remoteUri;
        std::string remoteDisplayName;
        CallDirection direction = CallDirection::Outbound;
        std::int64_t firstSeenAt = 0;
        std::int64_t startedAt = 0;
        bool connected = false;
        bool hasDirection = false;
    };

    std::unordered_map<CallId, Session> m_sessions;

    Session &upsertFromEntry(const CallEntry &entry, std::int64_t nowMs);
};

} // namespace compactphone::sip

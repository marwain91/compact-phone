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

    // True while an outbound call placed to this exact dialed target is still
    // live (recorded by noteOutbound, dropped on Disconnected). Used to dedupe
    // repeated dial requests so hammering Enter / Call does not stack multiple
    // identical calls. Matches the dialed string, not PJSIP's later-reported
    // remote URI, so an in-flight state callback cannot defeat the guard.
    bool hasLiveOutboundTo(const std::string &dialedTarget) const;

private:
    struct Session {
        AccountId accountId = kInvalidAccountId;
        std::string remoteUri;
        // The exact string the user dialed. Set once by noteOutbound and never
        // overwritten by state-callback upserts, so dedup stays stable across a
        // call's lifetime. Empty for inbound sessions.
        std::string dialedTarget;
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

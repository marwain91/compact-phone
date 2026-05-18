#include "CallSessionTracker.h"

#include <algorithm>

namespace compactphone::sip {

void CallSessionTracker::noteOutbound(CallId id,
                                      AccountId accountId,
                                      const std::string &remoteUri,
                                      std::int64_t nowMs)
{
    auto &s = m_sessions[id];
    s.accountId = accountId;
    s.remoteUri = remoteUri;
    s.direction = CallDirection::Outbound;
    s.hasDirection = true;
    if (s.firstSeenAt == 0) s.firstSeenAt = nowMs;
}

void CallSessionTracker::noteIncoming(const CallEntry &entry, std::int64_t nowMs)
{
    auto &s = upsertFromEntry(entry, nowMs);
    s.direction = CallDirection::Inbound;
    s.hasDirection = true;
}

std::optional<HistoryEntry> CallSessionTracker::noteState(const CallEntry &entry,
                                                          CallState state,
                                                          std::int64_t nowMs)
{
    auto &s = upsertFromEntry(entry, nowMs);

    if (state == CallState::Confirmed) {
        if (!s.connected) {
            s.connected = true;
            s.startedAt = nowMs;
        }
        return std::nullopt;
    }

    if (state != CallState::Disconnected) {
        return std::nullopt;
    }

    if (s.accountId == kInvalidAccountId || s.remoteUri.empty()) {
        m_sessions.erase(entry.id);
        return std::nullopt;
    }

    HistoryEntry h;
    h.accountId = s.accountId;
    h.direction = s.direction;
    h.remoteUri = s.remoteUri;
    h.remoteDisplayName = s.remoteDisplayName;
    h.startedAt = s.connected && s.startedAt > 0
                      ? s.startedAt
                      : (s.firstSeenAt > 0 ? s.firstSeenAt : nowMs);
    h.durationMs = s.connected && s.startedAt > 0
                       ? std::max<std::int64_t>(0, nowMs - s.startedAt)
                       : 0;

    m_sessions.erase(entry.id);
    return h;
}

void CallSessionTracker::erase(CallId id)
{
    m_sessions.erase(id);
}

CallSessionTracker::Session &CallSessionTracker::upsertFromEntry(
    const CallEntry &entry,
    std::int64_t nowMs)
{
    auto &s = m_sessions[entry.id];
    if (s.firstSeenAt == 0) s.firstSeenAt = nowMs;
    if (entry.accountId != kInvalidAccountId) s.accountId = entry.accountId;
    if (!entry.remoteUri.empty()) s.remoteUri = entry.remoteUri;
    if (!entry.remoteDisplayName.empty()) {
        s.remoteDisplayName = entry.remoteDisplayName;
    }
    const bool hasEntryDetails = entry.accountId != kInvalidAccountId
        || !entry.remoteUri.empty()
        || !entry.remoteDisplayName.empty();
    if (hasEntryDetails || !s.hasDirection) {
        s.direction = entry.direction;
        s.hasDirection = true;
    }
    return s;
}

} // namespace compactphone::sip

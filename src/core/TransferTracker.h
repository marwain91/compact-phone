#pragma once

#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>

namespace compactphone::sip {

// Tracks which calls should be hung up once a REFER/transfer finishes.
// Extracted from CallManager to isolate the transfer-cleanup bookkeeping;
// CallManager still owns the call map and performs the actual hangups.
// Keys match CallManager::CallId (std::int32_t).
class TransferTracker {
public:
    // Remember that finishing the transfer keyed by `id` should clean up
    // `cleanupIds` (the originating call, plus the consultation call for an
    // attended transfer).
    void record(std::int32_t id, std::vector<std::int32_t> cleanupIds)
    {
        m_pending[id] = std::move(cleanupIds);
    }

    bool has(std::int32_t id) const { return m_pending.count(id) > 0; }

    // Remove and return the cleanup ids for `id` (empty if none pending).
    std::vector<std::int32_t> take(std::int32_t id)
    {
        auto it = m_pending.find(id);
        if (it == m_pending.end()) return {};
        auto ids = std::move(it->second);
        m_pending.erase(it);
        return ids;
    }

    // Resolves a transfer-progress NOTIFY into the calls to hang up, mirroring
    // the REFER state machine:
    // - a non-final NOTIFY decides nothing and leaves the entry pending;
    // - a final NOTIFY consumes the entry whatever the outcome (the REFER
    //   subscription is over, so a stray later NOTIFY must find nothing);
    // - only a final 2xx returns the recorded legs — on failure (486 busy,
    //   603 declined, 3xx redirect) the user keeps talking, and hanging up
    //   the originating leg would tear down a live conversation.
    std::vector<std::int32_t> takeLegsToHangup(std::int32_t id, int statusCode,
                                               bool finalNotify)
    {
        if (!finalNotify) return {};
        auto ids = take(id);
        if (statusCode < 200 || statusCode >= 300) return {};
        return ids;
    }

    void drop(std::int32_t id) { m_pending.erase(id); }

private:
    std::unordered_map<std::int32_t, std::vector<std::int32_t>> m_pending;
};

} // namespace compactphone::sip

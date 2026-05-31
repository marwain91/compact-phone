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

    void drop(std::int32_t id) { m_pending.erase(id); }

private:
    std::unordered_map<std::int32_t, std::vector<std::int32_t>> m_pending;
};

} // namespace compactphone::sip

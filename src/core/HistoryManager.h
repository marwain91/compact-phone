#pragma once

#include "HistoryEntry.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

class HistoryManager {
public:
    // Retention policy. The call log is bounded so it cannot grow forever:
    // at most kMaxEntries rows are kept, and anything older than kMaxAgeMs
    // (relative to the newest call) is dropped. Pruned on every append.
    static constexpr int kMaxEntries = 50;
    static constexpr std::int64_t kMaxAgeMs = 90LL * 24 * 60 * 60 * 1000; // 90 days

    explicit HistoryManager(persistence::Database *db);

    HistoryManager(const HistoryManager &) = delete;
    HistoryManager &operator=(const HistoryManager &) = delete;

    // Append an entry. Returns the new id, or kInvalidHistoryId on failure.
    HistoryId append(const HistoryEntry &e);

    // Most recent first.
    std::vector<HistoryEntry> list(int limit = 200) const;

    std::optional<HistoryEntry> findById(HistoryId id) const;

private:
    // Enforce the retention policy. referenceNowMs anchors the age cutoff —
    // we pass the newest call's timestamp so retention tracks activity rather
    // than the wall clock (and stays robust to clock skew / test fixtures).
    void prune(std::int64_t referenceNowMs);

    persistence::Database *m_db;
};

} // namespace compactphone::sip

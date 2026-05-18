#pragma once

#include "HistoryEntry.h"

#include <optional>
#include <vector>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

class HistoryManager {
public:
    explicit HistoryManager(persistence::Database *db);

    HistoryManager(const HistoryManager &) = delete;
    HistoryManager &operator=(const HistoryManager &) = delete;

    // Append an entry. Returns the new id, or kInvalidHistoryId on failure.
    HistoryId append(const HistoryEntry &e);

    // Most recent first.
    std::vector<HistoryEntry> list(int limit = 200) const;

    std::optional<HistoryEntry> findById(HistoryId id) const;

private:
    persistence::Database *m_db;
};

} // namespace compactphone::sip

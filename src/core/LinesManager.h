#pragma once

#include "WatchedLine.h"

#include <QObject>

#include <memory>
#include <optional>
#include <vector>

namespace pj { class Buddy; }
namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

class AccountsManager;

// Manages the watched_lines table plus the PJSIP pj::Buddy lifecycle for
// each row (one Buddy per line, subscribed to "presence"). Emits
// linesChanged whenever a row is added/removed/edited or its presence
// state shifts.
class LinesManager : public QObject {
    Q_OBJECT
public:
    explicit LinesManager(persistence::Database *db,
                          AccountsManager *accounts,
                          QObject *parent = nullptr);
    ~LinesManager() override;

    LinesManager(const LinesManager &) = delete;
    LinesManager &operator=(const LinesManager &) = delete;

    // Returns the new id or kInvalidWatchedLineId on failure.
    WatchedLineId add(AccountId accountId,
                      const std::string &uri,
                      const std::string &label);
    bool remove(WatchedLineId id);

    std::vector<WatchedLine> list() const;
    std::optional<WatchedLine> find(WatchedLineId id) const;

    // Internal: AccountImpl::onBuddyState calls back here with the buddy
    // pointer; we look up which line that buddy belongs to and update
    // its cached state.
    void onBuddyStateChanged(pj::Buddy *buddy, LineState state);

signals:
    void linesChanged();

private:
    struct Entry;

    persistence::Database *m_db;
    AccountsManager *m_am;
    std::vector<std::unique_ptr<Entry>> m_entries;

    void loadFromDatabase();
    void subscribeAll();
    Entry *findByBuddy(pj::Buddy *buddy);
    Entry *findById(WatchedLineId id);
};

} // namespace compactphone::sip

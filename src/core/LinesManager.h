#pragma once

#include "WatchedLine.h"
#include "sipbackend/Types.h"

#include <QObject>

#include <memory>
#include <optional>
#include <vector>

namespace compactphone::persistence { class Database; }
namespace compactphone::sipbackend { class ISipBackend; }

namespace compactphone::sip {

class AccountsManager;

// Manages the watched_lines table and, for each row, a presence (BLF) watch
// through ISipBackend. The pj::Buddy SUBSCRIBE lifecycle lives in the PJSIP
// adapter (phase 5); LinesManager is main-thread-only and receives presence
// updates as the AccountsManager::presenceChanged signal (delivered queued on
// the main thread). Emits linesChanged whenever a row is added/removed or its
// presence state shifts.
class LinesManager : public QObject {
    Q_OBJECT
public:
    LinesManager(persistence::Database *db,
                 AccountsManager *accounts,
                 sipbackend::ISipBackend *backend = nullptr,
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

signals:
    void linesChanged();

private:
    struct Entry {
        WatchedLine line;
        // Backend watch handle for this line, or kInvalidWatchId if the line's
        // account is not registered yet (presence is best-effort at startup).
        sipbackend::WatchId watchId = sipbackend::kInvalidWatchId;
    };

    persistence::Database *m_db;
    AccountsManager *m_am;
    sipbackend::ISipBackend *m_backend;
    std::vector<std::unique_ptr<Entry>> m_entries;

    void loadFromDatabase();
    void subscribeAll();
    void watchEntry(Entry &e);
    // Connected to AccountsManager::presenceChanged. Maps PresenceState to the
    // watched line's LineState and emits linesChanged on a change.
    void onPresence(sipbackend::WatchId watchId,
                    sipbackend::PresenceState state);
};

} // namespace compactphone::sip

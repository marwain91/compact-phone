#pragma once

#include <vector>

namespace compactphone::sip {

struct CallEntry;

// Seam between CallsModel and the live call stack. CallsModel only needs the
// current set of calls as a flat snapshot; it does not need the whole
// CallManager (which routes into pjsua2 and cannot be constructed headless).
//
// CallManager implements this interface, so production wiring is unchanged
// (a CallManager* upcasts to CallSnapshotSource*). Unit tests provide a fake
// that returns canned CallEntry vectors, which lets CallsModel::refresh — the
// incremental insert/remove/dataChanged diff — be exercised without a SIP
// stack.
//
// Deliberately does NOT include CallEntry.h: that header includes CallManager.h
// (for CallId / CallState), and CallManager.h includes this one, so pulling in
// CallEntry.h here would form an include cycle. A forward declaration is enough
// because the snapshot() return type is only used by value in a declaration.
class CallSnapshotSource {
public:
    virtual ~CallSnapshotSource() = default;

    // Returns a copy of every active call's current state.
    virtual std::vector<CallEntry> snapshot() const = 0;
};

} // namespace compactphone::sip

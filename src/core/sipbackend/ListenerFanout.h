#pragma once

// Forwards every ISipBackendListener event to an ordered list of sinks.
// Phase 3 splits the listener role: AccountsManager consumes the account
// half (reg state, MWI, IM), CallManager the call half (incoming, call
// state, media, transfer). Order is pinned by the constructor caller —
// accounts BEFORE calls, so registration bookkeeping for an account is
// current before any call event referencing it is handled.
//
// Main-thread-only (events arrive queued per the boundary contract); holds
// non-owning pointers — the owner must uninstall this fanout from the
// backend (setListener(nullptr)) before any sink dies.

#include "ISipBackend.h"

#include <string>
#include <vector>

namespace compactphone::sipbackend {

class ListenerFanout : public ISipBackendListener {
public:
    explicit ListenerFanout(std::vector<ISipBackendListener *> sinks)
        : m_sinks(std::move(sinks)) {}

    void onRegState(AccountId id, bool active, int code,
                    const std::string &reason) override
    { for (auto *s : m_sinks) s->onRegState(id, active, code, reason); }
    void onIncomingCall(AccountId a, CallId c, const std::string &uri,
                        const std::string &dn) override
    { for (auto *s : m_sinks) s->onIncomingCall(a, c, uri, dn); }
    void onCallState(CallId c, CallState st, int code) override
    { for (auto *s : m_sinks) s->onCallState(c, st, code); }
    void onMediaState(CallId c, bool active, bool held) override
    { for (auto *s : m_sinks) s->onMediaState(c, active, held); }
    void onTransferStatus(CallId c, int code, bool fin,
                          const std::string &reason) override
    { for (auto *s : m_sinks) s->onTransferStatus(c, code, fin, reason); }
    void onMwi(AccountId a, int n, int o, bool act) override
    { for (auto *s : m_sinks) s->onMwi(a, n, o, act); }
    void onInstantMessage(AccountId a, const std::string &from,
                          const std::string &body) override
    { for (auto *s : m_sinks) s->onInstantMessage(a, from, body); }
    void onPresence(WatchId w, PresenceState st) override
    { for (auto *s : m_sinks) s->onPresence(w, st); }

private:
    std::vector<ISipBackendListener *> m_sinks;
};

} // namespace compactphone::sipbackend

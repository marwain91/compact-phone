#pragma once

#include <QNetworkInformation>
#include <QObject>

namespace compactphone {

// What a network event means for SIP: nothing, re-REGISTER (the source
// IP / NAT mapping changed), or registrations are gone until the network
// returns.
enum class NetworkAction {
    None,
    EmitBack, // -> networkBack(): accounts should re-REGISTER
    EmitLost, // -> networkLost(): connectivity dropped below Online
};

// Result of one reachabilityChanged event: the action plus the new
// online flag (always valid — equals the previous flag when action is
// None).
struct ReachabilityDecision {
    NetworkAction action = NetworkAction::None;
    bool online = false;
};

// Pure decision logic behind NetworkMonitor's QNetworkInformation
// lambdas, extracted so the debounce is unit-testable without a live
// backend. A regression here silently kills re-registration after
// wake/roam — lost inbound calls.

// Only Reachability::Online counts as online (Site/Local subnets can't
// reach the registrar). Repeats of the current state are absorbed: no
// action unless the online flag actually flips.
ReachabilityDecision decideReachabilityChange(
    bool wasOnline, QNetworkInformation::Reachability r);

// Transport flips (Wi-Fi -> Ethernet, switching SSIDs) invalidate the
// SIP transport's source IP even when reachability stays Online — but
// only matter while online; offline flips are absorbed (reachability
// coming back will emit networkBack itself).
NetworkAction decideTransportChange(bool online);

// Watches the host network reachability and transport medium via
// QNetworkInformation. Emits networkBack() when connectivity returns
// after a loss, or when the transport medium changes (Wi-Fi -> Ethernet,
// roaming between Wi-Fi networks) — both cases invalidate the SIP
// transport's source IP, so callers should re-register their accounts.
class NetworkMonitor : public QObject {
    Q_OBJECT
public:
    explicit NetworkMonitor(QObject *parent = nullptr);
    ~NetworkMonitor() override;

    // True if QNetworkInformation reports the host is reachable beyond
    // the local subnet. False if disconnected or backend unavailable.
    bool isOnline() const;

signals:
    // Fires when network reachability transitions to Online, or while
    // online the transport medium changes (so accounts should re-REGISTER
    // because the source IP / NAT mapping just changed).
    void networkBack();

    // Fires when reachability drops below Online.
    void networkLost();

private:
    bool m_online = false;
    bool m_backendOk = false;
};

} // namespace compactphone

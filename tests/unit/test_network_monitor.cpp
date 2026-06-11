#include <gtest/gtest.h>

#include "core/NetworkMonitor.h"

// decideReachabilityChange / decideTransportChange are the pure extraction
// of NetworkMonitor's QNetworkInformation lambdas. A regression here
// silently kills SIP re-registration after wake/roam (lost inbound calls)
// — previously this logic was only reachable through a live
// QNetworkInformation backend, so none of it was unit-tested.

using compactphone::decideReachabilityChange;
using compactphone::decideTransportChange;
using compactphone::NetworkAction;
using Reachability = QNetworkInformation::Reachability;

TEST(NetworkMonitorDecision, OfflineToOnlineEmitsBack)
{
    const auto d = decideReachabilityChange(false, Reachability::Online);
    EXPECT_EQ(d.action, NetworkAction::EmitBack);
    EXPECT_TRUE(d.online);
}

TEST(NetworkMonitorDecision, OnlineToBelowOnlineEmitsLost)
{
    // Anything below Online can't reach the registrar — Site/Local subnets
    // included, not just a hard disconnect.
    for (const auto r : {Reachability::Disconnected, Reachability::Local,
                         Reachability::Site, Reachability::Unknown}) {
        const auto d = decideReachabilityChange(true, r);
        EXPECT_EQ(d.action, NetworkAction::EmitLost)
            << "reachability " << static_cast<int>(r);
        EXPECT_FALSE(d.online);
    }
}

TEST(NetworkMonitorDecision, RepeatedOnlineIsAbsorbed)
{
    // The nowOnline == m_online debounce: backends re-announce the current
    // state (e.g. on metric changes); re-REGISTERing every time would churn
    // the registrar for nothing.
    const auto d = decideReachabilityChange(true, Reachability::Online);
    EXPECT_EQ(d.action, NetworkAction::None);
    EXPECT_TRUE(d.online); // flag must carry through unchanged
}

TEST(NetworkMonitorDecision, RepeatedOfflineIsAbsorbed)
{
    // Disconnected -> Site -> Local shuffles while offline are all "still
    // offline": no spurious networkLost storm, flag stays false.
    for (const auto r : {Reachability::Disconnected, Reachability::Local,
                         Reachability::Site, Reachability::Unknown}) {
        const auto d = decideReachabilityChange(false, r);
        EXPECT_EQ(d.action, NetworkAction::None)
            << "reachability " << static_cast<int>(r);
        EXPECT_FALSE(d.online);
    }
}

TEST(NetworkMonitorDecision, TransportChangeWhileOnlineEmitsBack)
{
    // Wi-Fi -> Ethernet (or an SSID roam) keeps reachability Online but
    // changes the source IP / NAT mapping — accounts must re-REGISTER.
    EXPECT_EQ(decideTransportChange(true), NetworkAction::EmitBack);
}

TEST(NetworkMonitorDecision, TransportChangeWhileOfflineIsAbsorbed)
{
    // Offline transport flips must not emit: the reachability transition
    // back to Online emits networkBack itself, and emitting while offline
    // would trigger doomed REGISTER attempts.
    EXPECT_EQ(decideTransportChange(false), NetworkAction::None);
}

#include "NetworkMonitor.h"

#include <QNetworkInformation>

#include <spdlog/spdlog.h>

namespace compactphone {

ReachabilityDecision decideReachabilityChange(
    bool wasOnline, QNetworkInformation::Reachability r)
{
    const bool nowOnline = r == QNetworkInformation::Reachability::Online;
    if (nowOnline == wasOnline) {
        return {NetworkAction::None, wasOnline};
    }
    return {nowOnline ? NetworkAction::EmitBack : NetworkAction::EmitLost,
            nowOnline};
}

NetworkAction decideTransportChange(bool online)
{
    return online ? NetworkAction::EmitBack : NetworkAction::None;
}

NetworkMonitor::NetworkMonitor(QObject *parent)
    : QObject(parent)
{
    m_backendOk = QNetworkInformation::loadDefaultBackend();
    if (!m_backendOk) {
        spdlog::warn("NetworkMonitor: no QNetworkInformation backend; "
                     "network change notifications disabled");
        return;
    }

    auto *info = QNetworkInformation::instance();
    m_online = info->reachability() == QNetworkInformation::Reachability::Online;

    connect(info, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability r) {
        const auto d = decideReachabilityChange(m_online, r);
        m_online = d.online;
        if (d.action == NetworkAction::EmitBack) {
            spdlog::info("NetworkMonitor: network back");
            emit networkBack();
        } else if (d.action == NetworkAction::EmitLost) {
            spdlog::info("NetworkMonitor: network lost (reachability={})",
                         static_cast<int>(r));
            emit networkLost();
        }
    });

    connect(info, &QNetworkInformation::transportMediumChanged, this,
            [this](QNetworkInformation::TransportMedium m) {
        if (decideTransportChange(m_online) == NetworkAction::EmitBack) {
            spdlog::info("NetworkMonitor: transport medium changed to {}",
                         static_cast<int>(m));
            emit networkBack();
        }
    });
}

NetworkMonitor::~NetworkMonitor() = default;

bool NetworkMonitor::isOnline() const
{
    return m_online;
}

} // namespace compactphone

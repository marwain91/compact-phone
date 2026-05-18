#include "NetworkMonitor.h"

#include <QNetworkInformation>

#include <spdlog/spdlog.h>

namespace compactphone {

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
        const bool nowOnline = r == QNetworkInformation::Reachability::Online;
        if (nowOnline == m_online) return;
        m_online = nowOnline;
        if (m_online) {
            spdlog::info("NetworkMonitor: network back");
            emit networkBack();
        } else {
            spdlog::info("NetworkMonitor: network lost (reachability={})",
                         static_cast<int>(r));
            emit networkLost();
        }
    });

    // Transport flips (Wi-Fi -> Ethernet, switching SSIDs) also invalidate
    // the SIP transport's source IP even when reachability stays Online.
    connect(info, &QNetworkInformation::transportMediumChanged, this,
            [this](QNetworkInformation::TransportMedium m) {
        if (!m_online) return;
        spdlog::info("NetworkMonitor: transport medium changed to {}",
                     static_cast<int>(m));
        emit networkBack();
    });
}

NetworkMonitor::~NetworkMonitor() = default;

bool NetworkMonitor::isOnline() const
{
    return m_online;
}

} // namespace compactphone

#pragma once

#include <QObject>

namespace compactphone {

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

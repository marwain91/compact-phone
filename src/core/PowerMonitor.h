#pragma once

#include <QObject>

#include <memory>

namespace compactphone {

// Cross-platform "system woke up from sleep" watcher. NetworkMonitor
// covers transport changes (Wi-Fi → Ethernet, IP renumbering) but not
// the reliable "your laptop just opened" signal — some OSes leave the
// reported network state intact across a sleep cycle even though the
// SIP socket is dead.
//
// Backends:
//   macOS   → NSWorkspaceDidWakeNotification
//   Windows → WM_POWERBROADCAST (PBT_APMRESUMESUSPEND)  — TODO
//   Linux   → org.freedesktop.login1 PrepareForSleep DBus — TODO
//
// Until the Windows / Linux backends ship, this class is a no-op there
// and the network watcher keeps the re-REGISTER loop sound.
class PowerMonitor : public QObject {
    Q_OBJECT
public:
    explicit PowerMonitor(QObject *parent = nullptr);
    ~PowerMonitor() override;

signals:
    // Emitted after the OS resumes from sleep. Listeners should re-REGISTER
    // every SIP account.
    void wokeUp();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace compactphone

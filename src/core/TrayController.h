#pragma once

#include <QObject>
#include <QString>

#include <memory>

#ifndef COMPACTPHONE_WITH_TRAY
#define COMPACTPHONE_WITH_TRAY 0
#endif

class QSystemTrayIcon;
class QMenu;
class QAction;

namespace compactphone {

class TrayController : public QObject {
    Q_OBJECT
public:
    explicit TrayController(QObject *parent = nullptr);
    ~TrayController() override;

    bool isAvailable() const;

    // Sets the on-icon badge shown for missed calls. 0 = no badge.
    // Also updates the macOS dock badge.
    void setMissedCallCount(int n);

    // Notify the user (toast + tray bubble + dock bounce on macOS).
    void notifyIncomingCall(const QString &from);

    // Convenience for the snackbar/transient feedback.
    void notify(const QString &title, const QString &message);

signals:
    // Emitted when the user wants the main window shown / hidden / app
    // quit from the tray menu.
    void showRequested();
    void hideRequested();
    void quitRequested();

private:
#if COMPACTPHONE_WITH_TRAY
    std::unique_ptr<QSystemTrayIcon> m_icon;
    QMenu *m_menu = nullptr;
    QAction *m_showAction = nullptr;
    QAction *m_hideAction = nullptr;
    QAction *m_quitAction = nullptr;
#endif
    int m_missedCallCount = 0;

    void rebuildTooltip();
};

} // namespace compactphone

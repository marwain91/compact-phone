#include "TrayController.h"

#if COMPACTPHONE_WITH_TRAY
#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPixmap>
#include <QSystemTrayIcon>

#include <spdlog/spdlog.h>
#endif

namespace compactphone {

#if COMPACTPHONE_WITH_TRAY
namespace {

// Build a filled monochrome phone silhouette so the tray icon matches
// the dock icon and the in-app brand mark (which both fill the same
// Lucide path). macOS uses this as a template image — the system tints
// the silhouette to match the menu bar's light/dark mode.
QIcon buildPhoneIcon()
{
    QPixmap pm(22, 22);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setPen(Qt::NoPen);
    p.setBrush(QBrush(Qt::black));
    // Lucide phone glyph, scaled to 22x22 (Lucide's natural viewBox is 24).
    const qreal s = 22.0 / 24.0;
    QPainterPath path;
    path.moveTo(20 * s, 14.92 * s);
    path.lineTo(20 * s, 17.92 * s);
    path.cubicTo(20 * s, 19.02 * s, 19.1 * s, 19.92 * s, 17.82 * s, 19.92 * s);
    path.cubicTo(11 * s, 19.92 * s, 4 * s, 13 * s, 4 * s, 6.18 * s);
    path.cubicTo(4 * s, 4.9 * s, 4.9 * s, 4 * s, 6 * s, 4 * s);
    path.lineTo(9 * s, 4 * s);
    path.cubicTo(10 * s, 4 * s, 10.85 * s, 4.72 * s, 11 * s, 5.72 * s);
    path.cubicTo(11.13 * s, 6.68 * s, 11.36 * s, 7.62 * s, 11.7 * s, 8.53 * s);
    path.cubicTo(11.84 * s, 8.93 * s, 11.75 * s, 9.38 * s, 11.45 * s, 9.68 * s);
    path.lineTo(10.09 * s, 11.04 * s);
    path.cubicTo(11.5 * s, 13.85 * s, 13.65 * s, 16 * s, 16.46 * s, 17.41 * s);
    path.lineTo(17.82 * s, 16.05 * s);
    path.cubicTo(18.12 * s, 15.75 * s, 18.57 * s, 15.66 * s, 18.97 * s, 15.8 * s);
    path.cubicTo(19.88 * s, 16.14 * s, 20.82 * s, 16.37 * s, 21.78 * s, 16.5 * s);
    path.closeSubpath();
    p.drawPath(path);
    p.end();
    QIcon icon(pm);
    icon.setIsMask(true);   // macOS template
    return icon;
}

} // namespace
#endif

TrayController::TrayController(QObject *parent) : QObject(parent)
{
#if COMPACTPHONE_WITH_TRAY
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        spdlog::warn("TrayController: system tray not available");
        return;
    }
    m_icon = std::make_unique<QSystemTrayIcon>();
    m_icon->setIcon(buildPhoneIcon());
    m_icon->setToolTip(QStringLiteral("CompactPhone"));

    m_menu = new QMenu;
    m_showAction = m_menu->addAction(tr("Show CompactPhone"));
    m_hideAction = m_menu->addAction(tr("Hide"));
    m_menu->addSeparator();
    m_quitAction = m_menu->addAction(tr("Quit"));

    connect(m_showAction, &QAction::triggered, this, &TrayController::showRequested);
    connect(m_hideAction, &QAction::triggered, this, &TrayController::hideRequested);
    connect(m_quitAction, &QAction::triggered, this, &TrayController::quitRequested);

    connect(m_icon.get(), &QSystemTrayIcon::activated, this,
            [this](QSystemTrayIcon::ActivationReason r) {
        if (r == QSystemTrayIcon::Trigger || r == QSystemTrayIcon::DoubleClick) {
            emit showRequested();
        }
    });
    connect(m_icon.get(), &QSystemTrayIcon::messageClicked, this,
            &TrayController::showRequested);

    m_icon->setContextMenu(m_menu);
    m_icon->show();
#endif
}

TrayController::~TrayController()
{
#if COMPACTPHONE_WITH_TRAY
    delete m_menu;
#endif
}

bool TrayController::isAvailable() const
{
#if COMPACTPHONE_WITH_TRAY
    return static_cast<bool>(m_icon);
#else
    return false;
#endif
}

void TrayController::setMissedCallCount(int n)
{
#if COMPACTPHONE_WITH_TRAY
    if (n < 0) n = 0;
    m_missedCallCount = n;
    rebuildTooltip();
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    if (auto *app = qobject_cast<QGuiApplication *>(QCoreApplication::instance())) {
        app->setBadgeNumber(n);
    }
#endif
#else
    Q_UNUSED(n);
#endif
}

void TrayController::notifyIncomingCall(const QString &from)
{
#if COMPACTPHONE_WITH_TRAY
    if (!m_icon) return;
    m_icon->showMessage(tr("Incoming call"),
                        from.isEmpty() ? tr("Unknown caller") : from,
                        QSystemTrayIcon::Information,
                        10000);
#else
    Q_UNUSED(from);
#endif
}

void TrayController::notify(const QString &title, const QString &message)
{
#if COMPACTPHONE_WITH_TRAY
    if (!m_icon) return;
    m_icon->showMessage(title, message, QSystemTrayIcon::Information, 4000);
#else
    Q_UNUSED(title);
    Q_UNUSED(message);
#endif
}

void TrayController::rebuildTooltip()
{
#if COMPACTPHONE_WITH_TRAY
    if (!m_icon) return;
    QString tip = QStringLiteral("CompactPhone");
    if (m_missedCallCount > 0) {
        tip += QStringLiteral(" — %1 missed").arg(m_missedCallCount);
    }
    m_icon->setToolTip(tip);
#endif
}

} // namespace compactphone

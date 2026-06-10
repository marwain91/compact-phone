#include "TrayController.h"

#if COMPACTPHONE_WITH_TRAY
#include <QAction>
#include <QApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QMenu>
#include <QPainter>
#include <QPixmap>
#include <QSvgRenderer>
#include <QSystemTrayIcon>

#include <spdlog/spdlog.h>
#endif

namespace compactphone {

#if COMPACTPHONE_WITH_TRAY
namespace {

// The exact Lucide phone path the dock icon and the in-app brand marks
// fill (branding/*.svg, src/ui/qml/Icons.qml). Keep the d-attribute
// verbatim — DaktelaBrandingLayout.TrayIconUsesTheBrandPhoneGlyph
// asserts it matches Icons.qml so the tray can't drift again.
constexpr const char *kPhoneGlyphSvg =
    "<svg xmlns='http://www.w3.org/2000/svg' viewBox='0 0 24 24' fill='black'>"
    "<path d='M22 16.92v3a2 2 0 0 1-2.18 2 19.79 19.79 0 0 1-8.63-3.07 19.5 19.5 0 0 1-6-6 19.79 19.79 0 0 1-3.07-8.67A2 2 0 0 1 4.11 2h3a2 2 0 0 1 2 1.72c.13.96.36 1.9.7 2.81a2 2 0 0 1-.45 2.11L8.09 9.91a16 16 0 0 0 6 6l1.27-1.27a2 2 0 0 1 2.11-.45c.91.34 1.85.57 2.81.7A2 2 0 0 1 22 16.92z'/>"
    "</svg>";

} // namespace

QImage TrayController::phoneGlyphImage(int size)
{
    QSvgRenderer renderer{QByteArray(kPhoneGlyphSvg)};
    QImage img(size, size, QImage::Format_ARGB32_Premultiplied);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    renderer.render(&p, QRectF(0, 0, size, size));
    p.end();
    return img;
}

// Filled monochrome phone silhouette. Black so macOS treats it as a
// template image and tints it to match the menu bar's light/dark mode.
// 22 px is the macOS menu bar size (44 covers Retina); 16/32 serve the
// Windows and Linux tray sizes.
QIcon TrayController::phoneTrayIcon()
{
    QIcon icon;
    for (const int size : {16, 22, 32, 44}) {
        icon.addPixmap(QPixmap::fromImage(phoneGlyphImage(size)));
    }
    icon.setIsMask(true);   // macOS template
    return icon;
}
#endif

TrayController::TrayController(QObject *parent) : QObject(parent)
{
#if COMPACTPHONE_WITH_TRAY
    if (!QSystemTrayIcon::isSystemTrayAvailable()) {
        spdlog::warn("TrayController: system tray not available");
        return;
    }
    m_icon = std::make_unique<QSystemTrayIcon>();
    m_icon->setIcon(phoneTrayIcon());
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

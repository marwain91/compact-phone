#include "AppActivationBridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QtGlobal>

namespace compactphone {

AppActivationBridge::AppActivationBridge(QCoreApplication *app, QObject *parent)
    : QObject(parent),
      m_app(app)
{
    if (m_app) {
        m_app->installEventFilter(this);
    }
    installMacDockReopenHandler(this);
}

AppActivationBridge::~AppActivationBridge()
{
    clearMacDockReopenHandler(this);
    if (m_app) {
        m_app->removeEventFilter(this);
    }
}

void AppActivationBridge::requestRestore()
{
    emit restoreRequested();
}

bool AppActivationBridge::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_app && event->type() == QEvent::ApplicationActivate) {
        requestRestore();
    }
    return QObject::eventFilter(watched, event);
}

#if !defined(Q_OS_MACOS)
void installMacDockReopenHandler(AppActivationBridge *bridge)
{
    Q_UNUSED(bridge);
}

void clearMacDockReopenHandler(AppActivationBridge *bridge)
{
    Q_UNUSED(bridge);
}
#endif

} // namespace compactphone

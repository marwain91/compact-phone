#include <gtest/gtest.h>

#include "core/AppActivationBridge.h"

#include <QCoreApplication>
#include <QEvent>
#include <QSignalSpy>

namespace {

QCoreApplication *ensureQApp()
{
    static int argc = 1;
    static char argv0[] = "test_app_activation_bridge";
    static char *argv[] = {argv0, nullptr};
    static QCoreApplication *app = nullptr;
    if (!QCoreApplication::instance()) {
        app = new QCoreApplication(argc, argv);
    }
    return QCoreApplication::instance();
}

} // namespace

TEST(AppActivationBridge, EmitsRestoreRequestWhenApplicationActivated)
{
    auto *app = ensureQApp();
    compactphone::AppActivationBridge bridge(app);
    QSignalSpy restoreRequests(
        &bridge, &compactphone::AppActivationBridge::restoreRequested);

    QEvent activate(QEvent::ApplicationActivate);
    QCoreApplication::sendEvent(app, &activate);

    EXPECT_EQ(restoreRequests.count(), 1);
}

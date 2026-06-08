#include "AppActivationBridge.h"

#include <QMetaObject>
#include <QPointer>
#include <QtGlobal>

#if defined(Q_OS_MACOS)
#import <AppKit/AppKit.h>
#import <objc/runtime.h>

namespace compactphone {
namespace {

QPointer<AppActivationBridge> g_bridge;
IMP g_originalReopenImp = nullptr;
bool g_hookInstalled = false;

BOOL compactphoneHandleReopen(id self, SEL command, NSApplication *application,
                              BOOL hasVisibleWindows)
{
    Q_UNUSED(hasVisibleWindows);
    if (g_bridge) {
        QMetaObject::invokeMethod(g_bridge.data(), "requestRestore",
                                  Qt::QueuedConnection);
    }

    if (g_originalReopenImp) {
        using Original = BOOL (*)(id, SEL, NSApplication *, BOOL);
        reinterpret_cast<Original>(g_originalReopenImp)(
            self, command, application, hasVisibleWindows);
    }
    return YES;
}

} // namespace

void installMacDockReopenHandler(AppActivationBridge *bridge)
{
    g_bridge = bridge;

    if (g_hookInstalled) {
        return;
    }

    NSApplication *application = [NSApplication sharedApplication];
    id delegate = [application delegate];
    if (!delegate) {
        return;
    }

    SEL selector = @selector(applicationShouldHandleReopen:hasVisibleWindows:);
    Class delegateClass = object_getClass(delegate);
    Method method = class_getInstanceMethod(delegateClass, selector);
    if (method) {
        g_originalReopenImp =
            method_setImplementation(method, (IMP)compactphoneHandleReopen);
        g_hookInstalled = true;
    } else {
        g_hookInstalled = class_addMethod(delegateClass, selector,
                                         (IMP)compactphoneHandleReopen,
                                         "c@:@c");
    }
}

void clearMacDockReopenHandler(AppActivationBridge *bridge)
{
    if (g_bridge == bridge) {
        g_bridge.clear();
    }
}

} // namespace compactphone
#endif

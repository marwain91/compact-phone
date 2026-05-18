#include "PowerMonitor.h"

#import <AppKit/AppKit.h>

namespace compactphone {

struct PowerMonitor::Impl {
    id observer = nil;
};

PowerMonitor::PowerMonitor(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
    NSNotificationCenter *nc =
        NSWorkspace.sharedWorkspace.notificationCenter;
    m_impl->observer = [nc
        addObserverForName:NSWorkspaceDidWakeNotification
                    object:nil
                     queue:NSOperationQueue.mainQueue
                usingBlock:^(NSNotification *) {
                    emit this->wokeUp();
                }];
}

PowerMonitor::~PowerMonitor()
{
    if (m_impl && m_impl->observer) {
        [NSWorkspace.sharedWorkspace.notificationCenter
            removeObserver:m_impl->observer];
    }
}

} // namespace compactphone

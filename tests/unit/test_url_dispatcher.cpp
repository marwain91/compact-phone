#include "core/UrlDispatcher.h"

#include <gtest/gtest.h>

#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QObject>
#include <QSignalSpy>
#include <QUrl>

#include <memory>

using compactphone::UrlDispatcher;

namespace {

// gtest_main doesn't create a QCoreApplication, but Qt event delivery
// (installEventFilter + sendEvent) needs one. Create a process-wide
// singleton lazily.
QCoreApplication *ensureQApp()
{
    static int argc = 1;
    static char argv0[] = "test_url_dispatcher";
    static char *argv[] = {argv0, nullptr};
    static std::unique_ptr<QCoreApplication> app;
    if (!QCoreApplication::instance()) {
        app = std::make_unique<QCoreApplication>(argc, argv);
    }
    return QCoreApplication::instance();
}

// Helper: install the dispatcher as an event filter on a throwaway
// QObject, then sendEvent through it — that's how QCoreApplication
// would route the QFileOpenEvent at runtime.
void feedUri(UrlDispatcher *d, const QString &uri)
{
    ensureQApp();
    QObject target;
    target.installEventFilter(d);
    QUrl url(uri);
    QFileOpenEvent ev(url);
    QCoreApplication::sendEvent(&target, &ev);
}

} // namespace

TEST(UrlDispatcherTest, TakePendingIsEmptyByDefault)
{
    auto *d = UrlDispatcher::instance();
    // Drain anything left over from earlier tests.
    while (!d->takePending().isEmpty()) {}
    EXPECT_TRUE(d->takePending().isEmpty());
}

TEST(UrlDispatcherTest, BuffersUriWhenNoListenerWired)
{
    auto *d = UrlDispatcher::instance();
    while (!d->takePending().isEmpty()) {}

    feedUri(d, QStringLiteral("sip:alice@example.com"));
    EXPECT_EQ(d->takePending(), "sip:alice@example.com");
    // takePending() clears the buffer.
    EXPECT_TRUE(d->takePending().isEmpty());
}

TEST(UrlDispatcherTest, EmitsSignalWhenListenerWired)
{
    auto *d = UrlDispatcher::instance();
    while (!d->takePending().isEmpty()) {}

    QSignalSpy spy(d, &UrlDispatcher::uriOpened);
    feedUri(d, QStringLiteral("tel:+15550101"));
    ASSERT_EQ(spy.count(), 1);
    EXPECT_EQ(spy.takeFirst().at(0).toString(), "tel:+15550101");
    // When a listener was present, the URI shouldn't have been buffered.
    EXPECT_TRUE(d->takePending().isEmpty());
}

TEST(UrlDispatcherTest, AcceptsAllSipSchemes)
{
    auto *d = UrlDispatcher::instance();
    while (!d->takePending().isEmpty()) {}

    QSignalSpy spy(d, &UrlDispatcher::uriOpened);
    feedUri(d, QStringLiteral("sip:alice@example.com"));
    feedUri(d, QStringLiteral("sips:bob@example.com"));
    feedUri(d, QStringLiteral("tel:+15550199"));
    feedUri(d, QStringLiteral("callto:1234"));
    EXPECT_EQ(spy.count(), 4);
}

#pragma once

#include <QObject>
#include <QString>

namespace compactphone {

// Singleton that intercepts QFileOpenEvent (delivered by Qt when macOS
// hands the app a sip:/sips:/tel:/callto: URL) and exposes the URI to
// the rest of the app. Buffers events that arrive before the controller
// is wired so the very first launch from a browser click still dials.
class UrlDispatcher : public QObject {
    Q_OBJECT
public:
    static UrlDispatcher *instance();

    // Pops and returns any URI that arrived before a listener was wired.
    // Returns an empty string if nothing was buffered.
    QString takePending();

signals:
    // Fires every time the OS asks us to open a sip:/tel:/callto: URI.
    // The argument is the raw URI as the OS passed it.
    void uriOpened(const QString &uri);

protected:
    bool eventFilter(QObject *obj, QEvent *e) override;

private:
    explicit UrlDispatcher(QObject *parent = nullptr);
    QString m_pending;
};

} // namespace compactphone

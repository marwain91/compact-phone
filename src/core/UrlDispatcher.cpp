#include "UrlDispatcher.h"

#include <QCoreApplication>
#include <QFileOpenEvent>
#include <QUrl>

#include <spdlog/spdlog.h>

namespace compactphone {

UrlDispatcher::UrlDispatcher(QObject *parent) : QObject(parent) {}

UrlDispatcher *UrlDispatcher::instance()
{
    static UrlDispatcher *s = nullptr;
    if (!s) s = new UrlDispatcher(QCoreApplication::instance());
    return s;
}

QString UrlDispatcher::takePending()
{
    QString out;
    std::swap(out, m_pending);
    return out;
}

bool UrlDispatcher::eventFilter(QObject *obj, QEvent *e)
{
    if (e->type() == QEvent::FileOpen) {
        auto *fo = static_cast<QFileOpenEvent *>(e);
        const QString uri = fo->url().isValid()
                                ? fo->url().toString()
                                : fo->file();
        if (!uri.isEmpty()) {
            spdlog::info("UrlDispatcher: incoming URI {}", uri.toStdString());
            // Buffer in case nobody is connected yet, AND fire the signal
            // so a wired-up listener can act immediately.
            if (receivers(SIGNAL(uriOpened(QString))) == 0) {
                m_pending = uri;
            }
            emit uriOpened(uri);
        }
        return true;
    }
    return QObject::eventFilter(obj, e);
}

} // namespace compactphone

#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QXmlStreamReader>

#include <spdlog/spdlog.h>

namespace compactphone {

namespace {
constexpr const char *kDefaultFeed =
    "https://compactphone.eu/appcast.xml";
}

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent),
      m_nam(new QNetworkAccessManager(this)),
      m_currentVersion(QCoreApplication::applicationVersion()),
      m_feedUrl(QString::fromUtf8(kDefaultFeed))
{
}

UpdateChecker::~UpdateChecker() = default;

void UpdateChecker::setFeedUrl(const QString &url)
{
    if (m_feedUrl == url) return;
    m_feedUrl = url;
    emit feedUrlChanged();
}

void UpdateChecker::check()
{
    QUrl url(m_feedUrl);
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CompactPhone/%1").arg(m_currentVersion));
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onReplyFinished(reply); });
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const auto parts = [](const QString &v) {
        QList<int> out;
        for (const auto &p : v.split('.')) out << p.toInt();
        return out;
    };
    const auto av = parts(a);
    const auto bv = parts(b);
    const int n = std::max(av.size(), bv.size());
    for (int i = 0; i < n; ++i) {
        const int x = i < av.size() ? av[i] : 0;
        const int y = i < bv.size() ? bv[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    return 0;
}

UpdateChecker::ParsedFeed UpdateChecker::parseAppcast(const QByteArray &xml)
{
    ParsedFeed best;
    QXmlStreamReader r(xml);
    while (!r.atEnd() && !r.hasError()) {
        if (r.readNext() != QXmlStreamReader::StartElement) continue;
        if (r.name() != QLatin1String("enclosure")) continue;

        const auto attrs = r.attributes();
        const QString shortVersion = attrs.value(
            "sparkle:shortVersionString").toString();
        const QString version = !shortVersion.isEmpty()
            ? shortVersion
            : attrs.value("sparkle:version").toString();
        const QString url = attrs.value("url").toString();
        if (version.isEmpty() || url.isEmpty()) continue;

        if (best.version.isEmpty()
            || compareVersions(best.version, version) < 0) {
            best.version = version;
            best.url = QUrl(url);
        }
    }
    return best;
}

void UpdateChecker::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply->error() != QNetworkReply::NoError) {
        const auto reason = reply->errorString();
        spdlog::warn("UpdateChecker: fetch failed: {}", reason.toStdString());
        emit checkFailed(reason);
        return;
    }

    const auto feed = parseAppcast(reply->readAll());
    if (feed.version.isEmpty()) {
        spdlog::warn("UpdateChecker: appcast had no <enclosure> entries");
        emit checkFailed(QStringLiteral("appcast had no entries"));
        return;
    }

    if (compareVersions(m_currentVersion, feed.version) >= 0) {
        spdlog::info("UpdateChecker: up to date ({} vs {})",
                     m_currentVersion.toStdString(),
                     feed.version.toStdString());
        emit upToDate();
        return;
    }

    m_latestVersion = feed.version;
    m_latestUrl = feed.url;
    spdlog::info("UpdateChecker: update available {} → {}",
                 m_currentVersion.toStdString(),
                 feed.version.toStdString());
    emit updateAvailable(feed.version, feed.url);
}

} // namespace compactphone

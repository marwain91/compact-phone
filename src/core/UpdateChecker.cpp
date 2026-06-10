#include "UpdateChecker.h"

#include <QCoreApplication>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringView>
#include <QXmlStreamReader>
#include <QtGlobal>

#include <algorithm>

#include <spdlog/spdlog.h>

namespace compactphone {

namespace {
// Per-OS appcast lives as an asset on every GitHub Release; /latest/
// redirects to whatever the newest tag is, so the URL never changes
// even as versions roll. The release workflows generate and upload
// these via tools/release/generate-appcast.py.
constexpr const char *kDefaultFeed =
#if defined(Q_OS_MACOS)
    "https://github.com/marwain91/compact-phone/releases/latest/download/appcast-macos.xml";
#elif defined(Q_OS_WIN)
    "https://github.com/marwain91/compact-phone/releases/latest/download/appcast-windows.xml";
#else
    "https://github.com/marwain91/compact-phone/releases/latest/download/appcast-linux.xml";
#endif

// HTTPS only — never accept a cleartext http:// feed or download. The
// appcast is not signature-verified, so transport security is the only thing
// standing between the user and an attacker-supplied installer.
bool isHttpsUrl(const QUrl &url)
{
    return url.isValid()
        && !url.isEmpty()
        && !url.host().isEmpty()
        && url.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) == 0;
}

// Pin the download (enclosure) host to GitHub's release infrastructure. Even
// if a feed is somehow tampered with or overridden, the download URL it
// advertises cannot point the user at an arbitrary host.
bool isTrustedDownloadHost(const QUrl &url)
{
    const QString host = url.host().toLower();
    return host == QLatin1String("github.com")
        || host.endsWith(QLatin1String(".github.com"))
        || host.endsWith(QLatin1String(".githubusercontent.com"));
}
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
    if (m_feedUrl.isEmpty()) {
        // No appcast URL configured (Linux today). Treat as "up to
        // date" so the UI doesn't loop on an error state and the
        // user just doesn't get an offer to update.
        spdlog::info("UpdateChecker: no feed URL configured, skipping");
        emit upToDate();
        return;
    }
    QUrl url(m_feedUrl, QUrl::StrictMode);
    if (!isHttpsUrl(url)) {
        spdlog::warn("UpdateChecker: invalid feed URL (https required): {}",
                     m_feedUrl.toStdString());
        emit checkFailed(QStringLiteral("invalid update feed URL"));
        return;
    }
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  QStringLiteral("CompactPhone/%1").arg(m_currentVersion));
    auto *reply = m_nam->get(req);
    connect(reply, &QNetworkReply::finished, this,
            [this, reply] { onReplyFinished(reply); });
}

namespace {
// Orders prerelease suffixes with digit runs compared numerically
// ("test2" < "test10") and everything else compared lexically.
int naturalCompare(QStringView a, QStringView b)
{
    qsizetype i = 0, j = 0;
    while (i < a.size() && j < b.size()) {
        if (a[i].isDigit() && b[j].isDigit()) {
            qsizetype i2 = i, j2 = j;
            while (i2 < a.size() && a[i2].isDigit()) ++i2;
            while (j2 < b.size() && b[j2].isDigit()) ++j2;
            const qulonglong x = a.mid(i, i2 - i).toULongLong();
            const qulonglong y = b.mid(j, j2 - j).toULongLong();
            if (x != y) return x < y ? -1 : 1;
            i = i2;
            j = j2;
        } else {
            if (a[i] != b[j]) return a[i] < b[j] ? -1 : 1;
            ++i;
            ++j;
        }
    }
    if (i < a.size()) return 1;
    if (j < b.size()) return -1;
    return 0;
}

struct ParsedVersion {
    QList<int> release;
    QString prerelease; // empty = full release
};

ParsedVersion parseVersion(const QString &v)
{
    ParsedVersion out;
    // Semver shape: release tags like v0.1.10-test1 publish appcast version
    // "0.1.10-test1". The prerelease suffix must be split off before the
    // numeric parse — "10-test1".toInt() silently returned 0, making a
    // 0.1.10-test1 entry order as 0.1.0 (below every published release).
    // Build metadata after '+' never affects ordering.
    QString s = v;
    if (const auto plus = s.indexOf(QLatin1Char('+')); plus >= 0) {
        s.truncate(plus);
    }
    if (const auto dash = s.indexOf(QLatin1Char('-')); dash >= 0) {
        out.prerelease = s.mid(dash + 1);
        s.truncate(dash);
    }
    for (const auto &p : s.split(QLatin1Char('.'))) {
        bool ok = false;
        const int n = p.toInt(&ok);
        out.release << (ok && n >= 0 ? n : 0);
    }
    return out;
}
} // namespace

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    const auto av = parseVersion(a);
    const auto bv = parseVersion(b);
    const auto n = std::max(av.release.size(), bv.release.size());
    for (qsizetype i = 0; i < n; ++i) {
        const int x = i < av.release.size() ? av.release[i] : 0;
        const int y = i < bv.release.size() ? bv.release[i] : 0;
        if (x != y) return x < y ? -1 : 1;
    }
    // Equal release part: a prerelease sorts below its own full release
    // (0.1.10-test1 < 0.1.10), and two prereleases order naturally.
    if (av.prerelease.isEmpty() != bv.prerelease.isEmpty()) {
        return av.prerelease.isEmpty() ? 1 : -1;
    }
    return naturalCompare(av.prerelease, bv.prerelease);
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
        const QUrl downloadUrl(url, QUrl::StrictMode);
        if (version.isEmpty()
            || !isHttpsUrl(downloadUrl)
            || !isTrustedDownloadHost(downloadUrl)) continue;

        if (best.version.isEmpty()
            || compareVersions(best.version, version) < 0) {
            best.version = version;
            best.url = downloadUrl;
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

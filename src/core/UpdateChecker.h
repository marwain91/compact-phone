#pragma once

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

namespace compactphone {

// Lightweight appcast checker. Fetches an XML feed compatible with the
// Sparkle 2.x / WinSparkle schema, parses the latest <enclosure>, and
// emits `updateAvailable` if its version is newer than the running build.
//
// Intentionally does not download or install the update — that's the job
// of platform-native Sparkle (macOS) / WinSparkle (Windows). This class
// covers the "is there an update?" loop on Linux and gives us a single
// QML-visible signal everywhere.
class UpdateChecker : public QObject {
    Q_OBJECT
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY updateAvailable)
    Q_PROPERTY(QUrl latestUrl READ latestUrl NOTIFY updateAvailable)
    Q_PROPERTY(QString feedUrl READ feedUrl WRITE setFeedUrl NOTIFY feedUrlChanged)

public:
    explicit UpdateChecker(QObject *parent = nullptr);
    ~UpdateChecker() override;

    QString currentVersion() const { return m_currentVersion; }
    QString latestVersion() const { return m_latestVersion; }
    QUrl latestUrl() const { return m_latestUrl; }
    QString feedUrl() const { return m_feedUrl; }
    void setFeedUrl(const QString &url);

    Q_INVOKABLE void check();

    // Parse a Sparkle 2.x appcast XML payload. Returns the best (highest)
    // (version, downloadUrl) pair across <enclosure> entries, or empty
    // strings if the feed has no usable entry / is malformed. Static so
    // tests can exercise it without spinning up QNetworkAccessManager.
    struct ParsedFeed {
        QString version;
        QUrl url;
    };
    static ParsedFeed parseAppcast(const QByteArray &xml);

    // Returns -1 if a < b, 0 if equal, +1 if a > b. Compares dotted
    // numeric segments (1.2.3 vs 1.2.10 etc.); non-numeric segments
    // resolve to 0.
    static int compareVersions(const QString &a, const QString &b);

signals:
    void updateAvailable(const QString &version, const QUrl &url);
    void upToDate();
    void checkFailed(const QString &reason);
    void feedUrlChanged();

private:
    QNetworkAccessManager *m_nam;
    QString m_currentVersion;
    QString m_latestVersion;
    QUrl m_latestUrl;
    QString m_feedUrl;

    void onReplyFinished(QNetworkReply *reply);
};

} // namespace compactphone

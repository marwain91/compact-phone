#pragma once

#include "Provider.h"

#include <QJsonObject>
#include <QString>
#include <QUrl>
#include <QVariantMap>

class QNetworkAccessManager;
class QNetworkReply;

namespace compactphone::provisioning {

// Bootstraps a CompactPhone SIP account from a Daktela V6 instance.
// Login flow:
//   1. POST {host}/api/v6/login.json   form: username,password,only_token=1
//   2. GET  {host}/api/v6/whoim.json            header: X-AUTH-TOKEN
//      -> result.user.extensions[] holds the SIP device records (metadata,
//         no secret); we pick the first named one.
//   3. GET  {host}/api/v6/sipDevices/<name>.json header: X-AUTH-TOKEN
//      -> the device record incl. the SIP secret. If this is denied (the
//         account may lack permission to read it) we keep the step-2 metadata
//         and emit passwordRequired so the wizard can prompt for the secret.
//
// The parsing of each step is exposed as a static helper so it can be
// unit-tested without a network in the loop.
class DaktelaProvider : public Provider {
    Q_OBJECT
public:
    explicit DaktelaProvider(QObject *parent = nullptr);
    ~DaktelaProvider() override;

    static constexpr const char *kId = "daktela";

    QString id() const override { return QStringLiteral("daktela"); }
    QString displayName() const override;
    QString hostPlaceholder() const override
    { return QStringLiteral("https://your.daktela.com"); }
    QString markPath() const override
    { return QStringLiteral("qrc:/branding/daktela-mark-light.svg"); }
    QString markPathDark() const override
    { return QStringLiteral("qrc:/branding/daktela-mark-dark.svg"); }

    void setNetworkAccessManager(QNetworkAccessManager *nam) override;
    void provision(const QString &host,
                   const QString &username,
                   const QString &password) override;
    void provisionWithToken(const QString &host,
                            const QString &accessToken) override;
    void completeWithPassword(const QString &password) override;
    void discoverAuthMethods(const QString &host) override;

    // --- Static helpers (pure functions, unit-tested) ---

    static QUrl normalizeHost(const QString &raw);
    static QUrl loginUrl(const QUrl &host);
    static QUrl whoamiUrl(const QUrl &host);
    static QUrl sipDeviceUrl(const QUrl &host, const QString &extensionName);
    static QUrl globalSettingsUrl(const QUrl &host);

    static QJsonValue unwrapResult(const QByteArray &body, QString *err);
    static QString extractAccessToken(const QJsonValue &result, QString *err);
    static QString extractExtensionName(const QJsonValue &result, QString *err);
    // The full SIP device record from whoim's user.extensions[] (metadata only,
    // no secret). Used to build the account when the dedicated secret fetch is
    // denied. Returns an empty object when no usable extension is present.
    static QJsonObject extractExtensionRecord(const QJsonValue &result);
    static QVariantMap buildAccountParams(const QUrl &host,
                                          const QJsonValue &sipDevice,
                                          const QString &displayName);

    // The wizard-facing method list. Daktela does not expose a
    // desktop-friendly OAuth client (its OAuth redirect_uri is
    // hardcoded to the web frontend), so we only offer:
    //   1. Username + password — calls /api/v6/login.json
    //   2. Access token — user generates one in Daktela's web UI
    //                     and pastes it back here
    //
    // The /internal/globalsettings.json discovery probe is still
    // used to validate the hostname is a real Daktela instance, but
    // the auth method list is the same regardless of what SSO methods
    // the tenant has enabled — we can't drive any of them honestly
    // from a desktop client.
    static QVariantList defaultAuthMethods(const QUrl &host);

private:
    void onLoginReply(QNetworkReply *r);
    void onWhoamiReply(QNetworkReply *r);
    void onSipDeviceReply(QNetworkReply *r);
    void onDiscoveryReply(QNetworkReply *r, const QUrl &host);
    void startWhoamiFetch();
    void fail(const QString &message);
    // Stash partial account params (no secret) and ask the wizard for the
    // SIP password. Fails outright if there isn't even a usable username.
    void promptForPassword(const QVariantMap &partialParams);

    QNetworkAccessManager *m_nam = nullptr;
    bool m_ownsNam = false;
    QUrl m_host;
    QString m_username;
    QString m_password;
    QString m_accessToken;
    QString m_extensionName;
    QJsonObject m_extensionRecord;  // whoim device metadata, for the fallback
    QString m_displayName;
    QVariantMap m_pendingParams;    // awaiting a manually-typed SIP password
};

} // namespace compactphone::provisioning

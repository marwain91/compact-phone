#include "DaktelaProvider.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QUrlQuery>

#include <spdlog/spdlog.h>

namespace compactphone::provisioning {

namespace {

constexpr int kHttpTimeoutMs = 15000;

QString joinCodecs(const QJsonValue &codecVal)
{
    if (!codecVal.isArray()) return {};
    QStringList out;
    for (const auto &v : codecVal.toArray()) {
        const auto s = v.toString();
        if (!s.isEmpty()) out << s;
    }
    return out.join(QStringLiteral(","));
}

QString mapTransport(const QString &raw)
{
    const auto t = raw.toLower();
    if (t == QStringLiteral("tls")) return QStringLiteral("tls");
    if (t == QStringLiteral("tcp")) return QStringLiteral("tcp");
    return QStringLiteral("udp");
}

QString mapDtmf(const QString &raw)
{
    const auto t = raw.toLower();
    if (t == QStringLiteral("info") || t == QStringLiteral("sip-info")) return QStringLiteral("info");
    if (t == QStringLiteral("inband")) return QStringLiteral("inband");
    return QStringLiteral("rfc2833");
}

QString mapSrtp(const QJsonValue &v, const QString &transport)
{
    if (v.isBool()) return v.toBool() ? QStringLiteral("required") : QStringLiteral("disabled");
    return transport == QStringLiteral("tls") ? QStringLiteral("optional")
                                              : QStringLiteral("disabled");
}

} // namespace

DaktelaProvider::DaktelaProvider(QObject *parent) : Provider(parent) {}

DaktelaProvider::~DaktelaProvider()
{
    if (m_ownsNam) delete m_nam;
}

QString DaktelaProvider::displayName() const
{
    return tr("Daktela");
}

void DaktelaProvider::setNetworkAccessManager(QNetworkAccessManager *nam)
{
    if (m_ownsNam) {
        delete m_nam;
        m_ownsNam = false;
    }
    m_nam = nam;
}

QUrl DaktelaProvider::normalizeHost(const QString &raw)
{
    auto trimmed = raw.trimmed();
    if (trimmed.isEmpty()) return {};
    if (!trimmed.contains(QStringLiteral("://"))) {
        trimmed.prepend(QStringLiteral("https://"));
    }
    while (trimmed.endsWith('/')) trimmed.chop(1);
    QUrl u(trimmed);
    if (!u.isValid() || u.host().isEmpty()) return {};
    return u;
}

QUrl DaktelaProvider::loginUrl(const QUrl &host)
{
    QUrl u = host;
    u.setPath(host.path() + QStringLiteral("/api/v6/login.json"));
    return u;
}

// The access token is sent via the X-AUTH-TOKEN request header, never in the
// URL query string — query-string secrets leak into server access logs and
// proxies. See onTokenIssued / fetchSipDevice where the header is set.
QUrl DaktelaProvider::whoamiUrl(const QUrl &host)
{
    QUrl u = host;
    u.setPath(host.path() + QStringLiteral("/api/v6/whoim.json"));
    return u;
}

QUrl DaktelaProvider::sipDeviceUrl(const QUrl &host,
                                   const QString &extensionName)
{
    QUrl u = host;
    // Top-level sipDevices model (camelCase), keyed by the extension/device
    // name — NOT nested under /extensions/. The device record carries the SIP
    // secret (subject to the caller's read permission).
    u.setPath(host.path() + QStringLiteral("/api/v6/sipDevices/")
              + extensionName + QStringLiteral(".json"));
    return u;
}

QUrl DaktelaProvider::globalSettingsUrl(const QUrl &host)
{
    QUrl u = host;
    u.setPath(host.path() + QStringLiteral("/internal/globalsettings.json"));
    return u;
}

QJsonValue DaktelaProvider::unwrapResult(const QByteArray &body, QString *err)
{
    QJsonParseError pe{};
    const auto doc = QJsonDocument::fromJson(body, &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (err) *err = QObject::tr("Invalid JSON response from server.");
        return {};
    }
    const auto obj = doc.object();
    // V6 returns { result, error, time }.  error is usually [] on success.
    const auto errorVal = obj.value(QStringLiteral("error"));
    if (errorVal.isArray() && !errorVal.toArray().isEmpty()) {
        QStringList msgs;
        for (const auto &e : errorVal.toArray()) msgs << e.toString();
        if (err) *err = msgs.join(QStringLiteral("; "));
        return {};
    }
    if (errorVal.isObject() && !errorVal.toObject().isEmpty()) {
        if (err) *err = QJsonDocument(errorVal.toObject()).toJson(QJsonDocument::Compact);
        return {};
    }
    return obj.value(QStringLiteral("result"));
}

QString DaktelaProvider::extractAccessToken(const QJsonValue &result, QString *err)
{
    if (result.isString()) return result.toString();
    if (result.isObject()) {
        const auto obj = result.toObject();
        if (obj.contains(QStringLiteral("accessToken"))) {
            return obj.value(QStringLiteral("accessToken")).toString();
        }
        const auto sys = obj.value(QStringLiteral("_sys")).toObject();
        if (sys.contains(QStringLiteral("accessToken"))) {
            return sys.value(QStringLiteral("accessToken")).toString();
        }
    }
    if (err) *err = QObject::tr("Login succeeded but the server did not return a token.");
    return {};
}

QString DaktelaProvider::extractExtensionName(const QJsonValue &result, QString *err)
{
    if (!result.isObject()) {
        if (err) *err = QObject::tr("Unexpected whoim.json shape.");
        return {};
    }
    auto obj = result.toObject();
    if (obj.contains(QStringLiteral("user")) && obj.value(QStringLiteral("user")).isObject()) {
        obj = obj.value(QStringLiteral("user")).toObject();
    }

    // V6 whoim returns the user's SIP devices as user.extensions[] — an ARRAY
    // of sipDevices records (each `{ name, title, transport, ... }`). The SIP
    // device name we need for the credential fetch is each element's `name`.
    // Use the first element that carries one.
    const auto extsVal = obj.value(QStringLiteral("extensions"));
    if (extsVal.isArray()) {
        for (const auto &item : extsVal.toArray()) {
            if (!item.isObject()) continue;
            const auto name = item.toObject().value(QStringLiteral("name")).toString();
            if (!name.isEmpty()) return name;
        }
    }

    // Back-compat: some shapes expose a singular `extension` (string or object).
    const auto extVal = obj.value(QStringLiteral("extension"));
    if (extVal.isString()) return extVal.toString();
    if (extVal.isObject()) {
        const auto eo = extVal.toObject();
        for (const auto *k : {"name", "extension", "extension_sip_device"}) {
            const auto v = eo.value(QLatin1String(k));
            if (v.isString() && !v.toString().isEmpty()) return v.toString();
            if (v.isDouble()) return QString::number(static_cast<qint64>(v.toDouble()));
        }
    }
    if (err) *err = QObject::tr("Your Daktela account is not assigned to a SIP extension.");
    return {};
}

QJsonObject DaktelaProvider::extractExtensionRecord(const QJsonValue &result)
{
    if (!result.isObject()) return {};
    auto obj = result.toObject();
    if (obj.value(QStringLiteral("user")).isObject()) {
        obj = obj.value(QStringLiteral("user")).toObject();
    }
    const auto exts = obj.value(QStringLiteral("extensions"));
    if (exts.isArray()) {
        for (const auto &item : exts.toArray()) {
            if (!item.isObject()) continue;
            if (!item.toObject().value(QStringLiteral("name")).toString().isEmpty()) {
                return item.toObject();
            }
        }
    }
    // Back-compat singular shapes: a device object, or a bare name string.
    const auto extVal = obj.value(QStringLiteral("extension"));
    if (extVal.isObject()) return extVal.toObject();
    if (extVal.isString() && !extVal.toString().isEmpty()) {
        return QJsonObject{ {QStringLiteral("name"), extVal.toString()} };
    }
    return {};
}

QVariantMap DaktelaProvider::buildAccountParams(const QUrl &host,
                                                const QJsonValue &sipDevice,
                                                const QString &displayName)
{
    if (!sipDevice.isObject()) return {};
    const auto obj = sipDevice.toObject();
    const auto domain = host.host();
    const auto transport = mapTransport(obj.value(QStringLiteral("transport")).toString());
    const auto srtpMode = mapSrtp(obj.value(QStringLiteral("srtp")), transport);
    const auto dtmf = mapDtmf(obj.value(QStringLiteral("dtmfmode")).toString());
    const auto name = obj.value(QStringLiteral("name")).toString();
    const auto title = obj.value(QStringLiteral("title")).toString();

    QVariantMap p;
    p[QStringLiteral("provider")] = QStringLiteral("daktela");
    p[QStringLiteral("label")] = QObject::tr("Daktela — %1").arg(host.host());
    p[QStringLiteral("displayName")] = title.isEmpty() ? displayName : title;
    p[QStringLiteral("username")] = name;
    p[QStringLiteral("authUser")] = name;
    p[QStringLiteral("domain")] = domain;
    // The sipDevices record may expose the secret as "password" or, in the
    // Asterisk-derived shape, "secret". whoim's extensions[] carries neither
    // (secrets are redacted there) — that empty value is what drives the
    // manual-password fallback when the dedicated fetch is denied.
    auto secret = obj.value(QStringLiteral("password")).toString();
    if (secret.isEmpty()) secret = obj.value(QStringLiteral("secret")).toString();
    p[QStringLiteral("password")] = secret;
    p[QStringLiteral("transport")] = transport;
    p[QStringLiteral("srtpMode")] = srtpMode;
    p[QStringLiteral("dtmfMethod")] = dtmf;
    p[QStringLiteral("codecs")] = joinCodecs(obj.value(QStringLiteral("codec")));
    p[QStringLiteral("registerOnStartup")] = true;
    p[QStringLiteral("iceEnabled")] = false;
    return p;
}

QVariantList DaktelaProvider::defaultAuthMethods(const QUrl &host)
{
    QVariantMap pw;
    pw[QStringLiteral("id")]           = QStringLiteral("password");
    pw[QStringLiteral("kind")]         = QStringLiteral("password");
    pw[QStringLiteral("displayName")]  = QObject::tr("Username & password");
    pw[QStringLiteral("openUrl")]      = QString();
    pw[QStringLiteral("instructions")] = QString();

    QVariantMap tok;
    tok[QStringLiteral("id")]           = QStringLiteral("token");
    tok[QStringLiteral("kind")]         = QStringLiteral("token");
    tok[QStringLiteral("displayName")]  = QObject::tr("Access token");
    // openUrl points at the tenant's web UI so the wizard's
    // "Open in browser" button lands the user in their Daktela session,
    // from which they can navigate to Account → API tokens.
    tok[QStringLiteral("openUrl")]      = host.toString();
    tok[QStringLiteral("instructions")] =
        QObject::tr("Open Daktela in your browser, sign in, then create a "
                    "personal access token under Account → API tokens. "
                    "Paste it below to provision this device.");
    return { pw, tok };
}

// --- Network flow ---

void DaktelaProvider::discoverAuthMethods(const QString &rawHost)
{
    const auto host = normalizeHost(rawHost);
    if (!host.isValid()) {
        emit authMethodsFailed(rawHost,
            tr("That doesn't look like a valid server address."));
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
    QNetworkRequest req(globalSettingsUrl(host));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/json"));
    req.setTransferTimeout(kHttpTimeoutMs);

    auto *r = m_nam->get(req);
    QPointer<DaktelaProvider> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r, host] {
        if (self) self->onDiscoveryReply(r, host);
        r->deleteLater();
    });
}

void DaktelaProvider::onDiscoveryReply(QNetworkReply *r, const QUrl &host)
{
    const int httpStatus = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    spdlog::info("Daktela discovery: GET {} -> http {} (err={})",
                 r->url().toString().toStdString(), httpStatus,
                 r->error() == QNetworkReply::NoError ? "none" : r->errorString().toStdString());
    // Discovery is best-effort: we use it to confirm the host is a real
    // Daktela instance (catches typos early), but the auth method list
    // we hand back is always the same — Daktela's SSO methods aren't
    // drivable from a desktop client, so we only ever offer
    // username/password and access token paste.
    if (r->error() != QNetworkReply::NoError) {
        spdlog::info("Daktela discovery soft-failed ({}); proceeding anyway",
                     r->errorString().toStdString());
    } else {
        const auto body = r->readAll();
        spdlog::info("Daktela discovery body size = {} bytes", body.size());
        QString err;
        const auto result = unwrapResult(body, &err);
        if (!err.isEmpty()) {
            spdlog::info("Daktela discovery payload soft-failed ({}); proceeding anyway",
                         err.toStdString());
        }
    }
    emit authMethodsDiscovered(host.toString(), defaultAuthMethods(host));
}

void DaktelaProvider::provisionWithToken(const QString &rawHost,
                                         const QString &accessToken)
{
    m_host = normalizeHost(rawHost);
    m_username.clear();
    m_password.clear();
    m_accessToken = accessToken.trimmed();
    m_extensionName.clear();
    m_displayName.clear();

    if (!m_host.isValid() || m_accessToken.isEmpty()) {
        fail(tr("Server address and access token are required."));
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }
    emit tokenIssued(m_host.toString(), m_accessToken);
    startWhoamiFetch();
}

void DaktelaProvider::provision(const QString &rawHost,
                                const QString &username,
                                const QString &password)
{
    m_host = normalizeHost(rawHost);
    m_username = username.trimmed();
    m_password = password;
    m_accessToken.clear();
    m_extensionName.clear();
    m_displayName.clear();

    if (!m_host.isValid() || m_username.isEmpty() || m_password.isEmpty()) {
        fail(tr("Host, username, and password are required."));
        return;
    }
    if (!m_nam) {
        m_nam = new QNetworkAccessManager(this);
        m_ownsNam = true;
    }

    emit progress(QStringLiteral("logging-in"));

    QNetworkRequest req(loginUrl(m_host));
    req.setHeader(QNetworkRequest::ContentTypeHeader,
                  QStringLiteral("application/x-www-form-urlencoded"));
    req.setTransferTimeout(kHttpTimeoutMs);

    QUrlQuery form;
    form.addQueryItem(QStringLiteral("username"), m_username);
    form.addQueryItem(QStringLiteral("password"), m_password);
    form.addQueryItem(QStringLiteral("only_token"), QStringLiteral("1"));
    const auto body = form.toString(QUrl::FullyEncoded).toUtf8();

    auto *r = m_nam->post(req, body);
    QPointer<DaktelaProvider> self(this);
    connect(r, &QNetworkReply::finished, this, [self, r] {
        if (self) self->onLoginReply(r);
        r->deleteLater();
    });
}

void DaktelaProvider::onLoginReply(QNetworkReply *r)
{
    if (r->error() != QNetworkReply::NoError) {
        const int httpStatus = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 401 || httpStatus == 403) {
            fail(tr("Invalid Daktela username or password."));
        } else {
            fail(tr("Login failed: %1").arg(r->errorString()));
        }
        return;
    }
    QString err;
    const auto result = unwrapResult(r->readAll(), &err);
    if (!err.isEmpty()) { fail(tr("Login failed: %1").arg(err)); return; }
    m_accessToken = extractAccessToken(result, &err);
    if (m_accessToken.isEmpty()) { fail(err); return; }

    emit tokenIssued(m_host.toString(), m_accessToken);
    startWhoamiFetch();
}

void DaktelaProvider::startWhoamiFetch()
{
    emit progress(QStringLiteral("fetching-user"));
    QNetworkRequest req(whoamiUrl(m_host));
    req.setRawHeader("X-AUTH-TOKEN", m_accessToken.toUtf8());
    // Don't let a cross-host redirect replay the auth token to another origin.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::SameOriginRedirectPolicy);
    req.setTransferTimeout(kHttpTimeoutMs);
    auto *next = m_nam->get(req);
    QPointer<DaktelaProvider> self(this);
    connect(next, &QNetworkReply::finished, this, [self, next] {
        if (self) self->onWhoamiReply(next);
        next->deleteLater();
    });
}

void DaktelaProvider::onWhoamiReply(QNetworkReply *r)
{
    if (r->error() != QNetworkReply::NoError) {
        const int httpStatus = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (httpStatus == 401 || httpStatus == 403) {
            fail(tr("The access token was rejected. It may have expired."));
            return;
        }
        fail(tr("Could not fetch user profile: %1").arg(r->errorString()));
        return;
    }
    QString err;
    const auto result = unwrapResult(r->readAll(), &err);
    if (!err.isEmpty()) { fail(tr("Could not read user profile: %1").arg(err)); return; }

    // Capture the display name for fallback while we're here.
    if (result.isObject()) {
        const auto user = result.toObject().value(QStringLiteral("user")).toObject();
        m_displayName = user.value(QStringLiteral("title")).toString();
        if (m_displayName.isEmpty()) {
            m_displayName = user.value(QStringLiteral("name")).toString();
        }
    }

    m_extensionName = extractExtensionName(result, &err);
    if (m_extensionName.isEmpty()) { fail(err); return; }
    // Keep the device metadata so we can still build the account if the
    // dedicated secret fetch is denied (permissions) — see onSipDeviceReply.
    m_extensionRecord = extractExtensionRecord(result);

    emit progress(QStringLiteral("fetching-extension"));

    QNetworkRequest req(sipDeviceUrl(m_host, m_extensionName));
    req.setRawHeader("X-AUTH-TOKEN", m_accessToken.toUtf8());
    // Don't let a cross-host redirect replay the auth token to another origin.
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                     QNetworkRequest::SameOriginRedirectPolicy);
    req.setTransferTimeout(kHttpTimeoutMs);
    auto *next = m_nam->get(req);
    QPointer<DaktelaProvider> self(this);
    connect(next, &QNetworkReply::finished, this, [self, next] {
        if (self) self->onSipDeviceReply(next);
        next->deleteLater();
    });
}

void DaktelaProvider::onSipDeviceReply(QNetworkReply *r)
{
    if (r->error() != QNetworkReply::NoError) {
        // Reading the device record (which carries the SIP secret) commonly
        // needs admin rights. Rather than dead-end, keep the metadata whoim
        // already gave us and ask the user to type the SIP password.
        spdlog::info("Daktela provisioning: sipDevices fetch failed ({}); "
                     "prompting for the SIP password",
                     r->errorString().toStdString());
        promptForPassword(buildAccountParams(m_host, m_extensionRecord, m_displayName));
        return;
    }
    QString err;
    const auto result = unwrapResult(r->readAll(), &err);
    if (!err.isEmpty()) { fail(tr("Could not read SIP credentials: %1").arg(err)); return; }

    auto params = buildAccountParams(m_host, result, m_displayName);
    if (params.isEmpty() || params.value(QStringLiteral("username")).toString().isEmpty()) {
        // Detail call succeeded but yielded nothing usable — fall back to the
        // whoim metadata + a manually-entered password.
        promptForPassword(buildAccountParams(m_host, m_extensionRecord, m_displayName));
        return;
    }
    if (params.value(QStringLiteral("password")).toString().isEmpty()) {
        // Record returned but the secret was withheld → ask the user for it.
        spdlog::info("Daktela provisioning: device record omitted the secret; "
                     "prompting for the SIP password");
        promptForPassword(params);
        return;
    }

    emit progress(QStringLiteral("done"));
    emit provisioningSucceeded(params);
}

void DaktelaProvider::promptForPassword(const QVariantMap &partialParams)
{
    if (partialParams.isEmpty()
        || partialParams.value(QStringLiteral("username")).toString().isEmpty()) {
        fail(tr("Could not determine which SIP extension to set up."));
        return;
    }
    m_pendingParams = partialParams;
    m_pendingParams[QStringLiteral("password")] = QString();
    emit passwordRequired(m_pendingParams);
}

void DaktelaProvider::completeWithPassword(const QString &password)
{
    if (m_pendingParams.isEmpty()
        || m_pendingParams.value(QStringLiteral("username")).toString().isEmpty()) {
        fail(tr("There is no pending Daktela sign-in to finish."));
        return;
    }
    auto params = m_pendingParams;
    params[QStringLiteral("password")] = password;
    m_pendingParams.clear();
    emit progress(QStringLiteral("done"));
    emit provisioningSucceeded(params);
}

void DaktelaProvider::fail(const QString &message)
{
    spdlog::warn("Daktela provisioning failed: {}", message.toStdString());
    emit provisioningFailed(message);
}

} // namespace compactphone::provisioning

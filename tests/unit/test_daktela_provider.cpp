#include "core/provisioning/DaktelaProvider.h"

#include <gtest/gtest.h>

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

using compactphone::provisioning::DaktelaProvider;

namespace {

QJsonValue parseJsonAsValue(const QByteArray &body)
{
    QString err;
    return DaktelaProvider::unwrapResult(body, &err);
}

} // namespace

TEST(DaktelaProviderTest, NormalizeHostPrependsHttpsAndStripsTrailingSlash)
{
    const auto a = DaktelaProvider::normalizeHost("acme.daktela.com");
    EXPECT_EQ(a.scheme(), "https");
    EXPECT_EQ(a.host(), "acme.daktela.com");

    const auto b = DaktelaProvider::normalizeHost("http://acme.daktela.com/");
    EXPECT_EQ(b.scheme(), "http");
    EXPECT_FALSE(b.toString().endsWith("/"));

    const auto c = DaktelaProvider::normalizeHost("https://acme.daktela.com:8443/");
    EXPECT_EQ(c.scheme(), "https");
    EXPECT_EQ(c.port(), 8443);
    EXPECT_EQ(c.host(), "acme.daktela.com");
}

TEST(DaktelaProviderTest, NormalizeHostRejectsBlank)
{
    EXPECT_FALSE(DaktelaProvider::normalizeHost("").isValid());
    EXPECT_FALSE(DaktelaProvider::normalizeHost("   ").isValid());
}

TEST(DaktelaProviderTest, LoginUrlAppendsV6Path)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    const auto url = DaktelaProvider::loginUrl(host);
    EXPECT_EQ(url.toString(), "https://acme.daktela.com/api/v6/login.json");
}

TEST(DaktelaProviderTest, WhoamiUrlEncodesTokenAsQueryParam)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    const auto url = DaktelaProvider::whoamiUrl(host, "abc123");
    EXPECT_EQ(url.path(), "/api/v6/whoim.json");
    QUrlQuery q(url);
    EXPECT_EQ(q.queryItemValue("accessToken"), "abc123");
}

TEST(DaktelaProviderTest, SipDeviceUrlEmbedsExtensionInPath)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    const auto url = DaktelaProvider::sipDeviceUrl(host, "1001", "tok");
    EXPECT_EQ(url.path(), "/api/v6/extensions/sipdevices/1001.json");
    QUrlQuery q(url);
    EXPECT_EQ(q.queryItemValue("accessToken"), "tok");
}

TEST(DaktelaProviderTest, UnwrapResultReturnsResultOnSuccess)
{
    const QByteArray body =
        R"({"result":"abcdef","error":[],"time":1730000000})";
    QString err;
    const auto v = DaktelaProvider::unwrapResult(body, &err);
    EXPECT_TRUE(err.isEmpty());
    EXPECT_EQ(v.toString(), "abcdef");
}

TEST(DaktelaProviderTest, UnwrapResultReportsArrayErrors)
{
    const QByteArray body =
        R"({"result":null,"error":["Bad credentials","retry"]})";
    QString err;
    const auto v = DaktelaProvider::unwrapResult(body, &err);
    EXPECT_TRUE(v.isNull() || v.isUndefined());
    EXPECT_NE(err.indexOf("Bad credentials"), -1);
    EXPECT_NE(err.indexOf("retry"), -1);
}

TEST(DaktelaProviderTest, UnwrapResultRejectsMalformedJson)
{
    QString err;
    const auto v = DaktelaProvider::unwrapResult("not json", &err);
    EXPECT_TRUE(v.isNull() || v.isUndefined());
    EXPECT_FALSE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractAccessTokenAcceptsBareString)
{
    QString err;
    const auto t = DaktelaProvider::extractAccessToken(QJsonValue("tok-xyz"), &err);
    EXPECT_EQ(t, "tok-xyz");
    EXPECT_TRUE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractAccessTokenAcceptsObjectAccessToken)
{
    QJsonObject obj{ {"accessToken", "tok-xyz"} };
    QString err;
    const auto t = DaktelaProvider::extractAccessToken(obj, &err);
    EXPECT_EQ(t, "tok-xyz");
}

TEST(DaktelaProviderTest, ExtractAccessTokenAcceptsSysAccessToken)
{
    QJsonObject sys{ {"accessToken", "sysTok"} };
    QJsonObject user{ {"_sys", sys} };
    QString err;
    const auto t = DaktelaProvider::extractAccessToken(user, &err);
    EXPECT_EQ(t, "sysTok");
}

TEST(DaktelaProviderTest, ExtractAccessTokenReportsMissingToken)
{
    QString err;
    const auto t = DaktelaProvider::extractAccessToken(QJsonValue(42.0), &err);
    EXPECT_TRUE(t.isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractExtensionNameFromUserStringField)
{
    QJsonObject user{ {"name", "alice"}, {"extension", "1001"} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_EQ(DaktelaProvider::extractExtensionName(root, &err), "1001");
    EXPECT_TRUE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractExtensionNameFromUserObjectField)
{
    QJsonObject ext{ {"name", "2002"}, {"title", "Bob phone"} };
    QJsonObject user{ {"extension", ext} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_EQ(DaktelaProvider::extractExtensionName(root, &err), "2002");
}

TEST(DaktelaProviderTest, ExtractExtensionNameFailsWhenAbsent)
{
    QJsonObject user{ {"name", "alice"} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_TRUE(DaktelaProvider::extractExtensionName(root, &err).isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

TEST(DaktelaProviderTest, BuildAccountParamsMapsCoreFields)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{
        {"name", "1001"},
        {"title", "Alice Office"},
        {"password", "s3cret"},
        {"transport", "TLS"},
        {"srtp", true},
        {"dtmfmode", "rfc2833"},
        {"codec", QJsonArray{ "opus", "alaw", "ulaw" }}
    };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "Fallback Display");
    EXPECT_EQ(params.value("username").toString(), "1001");
    EXPECT_EQ(params.value("authUser").toString(), "1001");
    EXPECT_EQ(params.value("password").toString(), "s3cret");
    EXPECT_EQ(params.value("displayName").toString(), "Alice Office");
    EXPECT_EQ(params.value("domain").toString(), "acme.daktela.com");
    EXPECT_EQ(params.value("transport").toString(), "tls");
    EXPECT_EQ(params.value("srtpMode").toString(), "required");
    EXPECT_EQ(params.value("codecs").toString(), "opus,alaw,ulaw");
    EXPECT_EQ(params.value("dtmfMethod").toString(), "rfc2833");
    EXPECT_TRUE(params.value("registerOnStartup").toBool());
}

TEST(DaktelaProviderTest, BuildAccountParamsDefaultsTransportAndSrtp)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{ {"name", "9000"}, {"password", "x"} };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "Fallback");
    EXPECT_EQ(params.value("transport").toString(), "udp");
    EXPECT_EQ(params.value("srtpMode").toString(), "disabled");
    EXPECT_EQ(params.value("dtmfMethod").toString(), "rfc2833");
    // title missing → falls back to displayName parameter.
    EXPECT_EQ(params.value("displayName").toString(), "Fallback");
}

TEST(DaktelaProviderTest, BuildAccountParamsTLSWithUnspecifiedSrtpIsOptional)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{ {"name", "1001"}, {"transport", "tls"} };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "");
    EXPECT_EQ(params.value("transport").toString(), "tls");
    EXPECT_EQ(params.value("srtpMode").toString(), "optional");
}

TEST(DaktelaProviderTest, BuildAccountParamsReturnsEmptyForNonObject)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    EXPECT_TRUE(DaktelaProvider::buildAccountParams(host, QJsonValue(42.0), "").isEmpty());
}

TEST(DaktelaProviderTest, GlobalSettingsUrlPath)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    EXPECT_EQ(DaktelaProvider::globalSettingsUrl(host).path(),
              "/internal/globalsettings.json");
}

TEST(DaktelaProviderTest, DefaultAuthMethodsOffersPasswordAndToken)
{
    // Daktela does not expose a desktop-friendly OAuth client (redirect_uri
    // is hardcoded to the web frontend), so we always offer exactly two
    // honest methods: username+password and access-token paste. SSO method
    // tiles parsed from /internal/globalsettings.json's `authentications`
    // block are intentionally not surfaced.
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    const auto methods = DaktelaProvider::defaultAuthMethods(host);
    ASSERT_EQ(methods.size(), 2);
    EXPECT_EQ(methods.at(0).toMap().value("id").toString(), "password");
    EXPECT_EQ(methods.at(0).toMap().value("kind").toString(), "password");
    EXPECT_EQ(methods.at(1).toMap().value("id").toString(), "token");
    EXPECT_EQ(methods.at(1).toMap().value("kind").toString(), "token");
    // Token method's openUrl should be the tenant's web URL so the wizard's
    // "Open in browser" button lands the user where they can generate one.
    EXPECT_TRUE(methods.at(1).toMap().value("openUrl").toString()
                .startsWith("https://acme.daktela.com"));
    EXPECT_FALSE(methods.at(1).toMap().value("instructions").toString().isEmpty());
}

TEST(DaktelaProviderTest, EndToEndPayloadParsing)
{
    // Quick smoke test that chains the four helpers as they'd run in real
    // network use, against representative V6 payloads.
    const QByteArray loginBody =
        R"({"result":"tok-xyz","error":[],"time":1730000000})";
    const auto loginResult = parseJsonAsValue(loginBody);
    QString err;
    EXPECT_EQ(DaktelaProvider::extractAccessToken(loginResult, &err), "tok-xyz");

    const QByteArray whoamiBody =
        R"({"result":{"user":{"name":"alice","title":"Alice","extension":"1001"}},"error":[]})";
    const auto whoamiResult = parseJsonAsValue(whoamiBody);
    EXPECT_EQ(DaktelaProvider::extractExtensionName(whoamiResult, &err), "1001");

    const QByteArray sipBody =
        R"({"result":{"name":"1001","title":"Alice phone","password":"pw","transport":"udp","codec":["alaw"]},"error":[]})";
    const auto sipResult = parseJsonAsValue(sipBody);
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    const auto params = DaktelaProvider::buildAccountParams(host, sipResult, "Alice");
    EXPECT_EQ(params.value("username").toString(), "1001");
    EXPECT_EQ(params.value("password").toString(), "pw");
    EXPECT_EQ(params.value("displayName").toString(), "Alice phone");
    EXPECT_EQ(params.value("codecs").toString(), "alaw");
}

// --- buildAccountParams: media-security mapping branches -------------------
// mapSrtp/mapDtmf/mapTransport decide the account's media-security posture from
// untrusted provisioning data. The happy paths are covered above; these pin
// the branches that were not — getting them wrong silently weakens encryption
// or DTMF signalling.

TEST(DaktelaProviderTest, BuildAccountParamsExplicitSrtpFalseDisablesEvenOnTls)
{
    // A server that explicitly says srtp:false must yield "disabled", NOT the
    // TLS-default "optional". Pinning false != optional is the whole point.
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{ {"name", "1001"}, {"transport", "tls"}, {"srtp", false} };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "X");
    EXPECT_EQ(params.value("srtpMode").toString(), "disabled");
}

TEST(DaktelaProviderTest, BuildAccountParamsExplicitSrtpTrueRequiredOnUdp)
{
    // The bool branch wins over transport: srtp:true is "required" even on udp.
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{ {"name", "1001"}, {"transport", "udp"}, {"srtp", true} };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "X");
    EXPECT_EQ(params.value("srtpMode").toString(), "required");
}

TEST(DaktelaProviderTest, BuildAccountParamsMapsInfoDtmfModes)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    for (const char *raw : {"info", "sip-info"}) {
        QJsonObject sip{ {"name", "1001"}, {"dtmfmode", raw} };
        const auto params = DaktelaProvider::buildAccountParams(host, sip, "X");
        EXPECT_EQ(params.value("dtmfMethod").toString(), "info")
            << "dtmfmode=" << raw;
    }
}

TEST(DaktelaProviderTest, BuildAccountParamsMapsInbandDtmfMode)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject sip{ {"name", "1001"}, {"dtmfmode", "inband"} };
    const auto params = DaktelaProvider::buildAccountParams(host, sip, "X");
    EXPECT_EQ(params.value("dtmfMethod").toString(), "inband");
}

TEST(DaktelaProviderTest, BuildAccountParamsMapsTcpTransportAndFoldsCase)
{
    const auto host = DaktelaProvider::normalizeHost("acme.daktela.com");
    QJsonObject tcp{ {"name", "1001"}, {"transport", "TCP"} };
    EXPECT_EQ(DaktelaProvider::buildAccountParams(host, tcp, "X")
                  .value("transport").toString(), "tcp");
    QJsonObject tls{ {"name", "1001"}, {"transport", "Tls"} };
    EXPECT_EQ(DaktelaProvider::buildAccountParams(host, tls, "X")
                  .value("transport").toString(), "tls");
}

// --- extractExtensionName: alt-key / numeric / non-object branches ---------
// Picks which SIP extension is provisioned — a wrong result means wrong
// identity/credentials. The string-and-name-key paths are covered above.

TEST(DaktelaProviderTest, ExtractExtensionNameFromAltKeyExtensionSipDevice)
{
    // extension object with neither "name" nor "extension", only the third
    // fallback key.
    QJsonObject ext{ {"extension_sip_device", "3003"} };
    QJsonObject user{ {"extension", ext} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_EQ(DaktelaProvider::extractExtensionName(root, &err), "3003");
    EXPECT_TRUE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractExtensionNameSkipsEmptyNameForNextKey)
{
    // An empty "name" must not be returned — the loop falls through to the
    // next candidate key (pins the !isEmpty() guard).
    QJsonObject ext{ {"name", ""}, {"extension", "4004"} };
    QJsonObject user{ {"extension", ext} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_EQ(DaktelaProvider::extractExtensionName(root, &err), "4004");
    EXPECT_TRUE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractExtensionNameFromNumericNameField)
{
    // A JSON number extension is coerced to its integer string form, not
    // dropped (the isDouble() branch).
    QJsonObject ext{ {"name", 1001} };
    QJsonObject user{ {"extension", ext} };
    QJsonObject root{ {"user", user} };
    QString err;
    EXPECT_EQ(DaktelaProvider::extractExtensionName(root, &err), "1001");
    EXPECT_TRUE(err.isEmpty());
}

TEST(DaktelaProviderTest, ExtractExtensionNameRejectsNonObjectResult)
{
    QString err;
    EXPECT_TRUE(
        DaktelaProvider::extractExtensionName(QJsonValue(3.14), &err).isEmpty());
    EXPECT_FALSE(err.isEmpty());
}

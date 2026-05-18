// Manual smoke test against a real Daktela instance. Not built by default;
// add to tests/CMakeLists.txt or build manually with qmake.
//
// Verifies that:
//   1. QNetworkAccessManager actually does TLS (catches the cert-only bug).
//   2. /internal/globalsettings.json returns a parseable authentications block.
//   3. DaktelaProvider::parseAuthMethods picks up Google / Azure / Daktela SSO.
//
// Exits 0 on success, non-zero with a diagnostic on the first failure.

#include "core/provisioning/DaktelaProvider.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QObject>
#include <QTimer>
#include <QUrl>

#include <cstdio>
#include <cstdlib>

int main(int argc, char **argv)
{
    QCoreApplication a(argc, argv);
    const QString host = (argc > 1) ? argv[1] : "daktela.daktela.com";
    using compactphone::provisioning::DaktelaProvider;
    const auto url = DaktelaProvider::globalSettingsUrl(DaktelaProvider::normalizeHost(host));

    QNetworkAccessManager nam;
    QNetworkRequest req(url);
    req.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");
    req.setTransferTimeout(15000);

    int rc = 1;
    auto *r = nam.get(req);
    QObject::connect(r, &QNetworkReply::finished, [&] {
        const int code = r->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        const auto e = r->error();
        const auto body = r->readAll();
        std::fprintf(stderr, "GET %s -> HTTP %d  err=%d (%s)  body=%lld bytes\n",
                     url.toString().toUtf8().constData(),
                     code, (int)e, r->errorString().toUtf8().constData(),
                     (long long)body.size());
        if (e != QNetworkReply::NoError) {
            std::fprintf(stderr, "FAIL: network error - TLS backend missing?\n");
            a.quit();
            return;
        }
        QString perr;
        const auto result = DaktelaProvider::unwrapResult(body, &perr);
        if (!perr.isEmpty()) {
            std::fprintf(stderr, "FAIL: unwrap: %s\n", perr.toUtf8().constData());
            a.quit();
            return;
        }
        const auto methods = DaktelaProvider::parseAuthMethods(
            result, DaktelaProvider::normalizeHost(host));
        std::fprintf(stderr, "Parsed %lld auth methods:\n", (long long)methods.size());
        for (const auto &m : methods) {
            const auto map = m.toMap();
            std::fprintf(stderr, "  - id=%s  kind=%s  displayName=%s\n",
                         map.value("id").toString().toUtf8().constData(),
                         map.value("kind").toString().toUtf8().constData(),
                         map.value("displayName").toString().toUtf8().constData());
        }
        rc = methods.size() > 1 ? 0 : 2;
        a.quit();
    });
    QObject::connect(&a, &QCoreApplication::aboutToQuit, [&] { std::fflush(stderr); });
    a.QCoreApplication::exec();
    return rc;
}

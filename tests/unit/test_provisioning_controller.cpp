#include <gtest/gtest.h>

#include "core/NoticeDuration.h"
#include "core/ProvisioningController.h"
#include "core/provisioning/Provider.h"
#include "core/provisioning/Registry.h"

#include <QSignalSpy>
#include <QString>
#include <QVariantList>
#include <QVariantMap>

#include <memory>
#include <utility>
#include <vector>

namespace {

// Records every delegated call and lets tests fire the Provider signals on
// demand (signals are protected for emission outside the class hierarchy).
// No Q_OBJECT: it adds no signals of its own, so the inherited metaobject is
// all the controller's connect() calls need.
class FakeProvider : public compactphone::provisioning::Provider {
public:
    QString id() const override { return QStringLiteral("fake"); }
    QString displayName() const override { return QStringLiteral("Fake"); }

    void provision(const QString &host, const QString &username,
                   const QString &password) override
    {
        ++provisionCalls;
        lastHost = host;
        lastUsername = username;
        lastPassword = password;
    }
    void provisionWithToken(const QString &host,
                            const QString &accessToken) override
    {
        ++tokenCalls;
        lastHost = host;
        lastToken = accessToken;
    }
    void completeWithPassword(const QString &password) override
    {
        ++manualPasswordCalls;
        lastPassword = password;
    }
    void discoverAuthMethods(const QString &host) override
    {
        ++discoverCalls;
        lastHost = host;
    }

    void fireSucceeded(const QVariantMap &params)
    {
        emit provisioningSucceeded(params);
    }
    void fireFailed(const QString &error) { emit provisioningFailed(error); }
    void fireProgress(const QString &stage) { emit progress(stage); }
    void firePasswordRequired(const QVariantMap &partial)
    {
        emit passwordRequired(partial);
    }
    void fireAuthMethodsDiscovered(const QString &host,
                                   const QVariantList &methods)
    {
        emit authMethodsDiscovered(host, methods);
    }
    void fireAuthMethodsFailed(const QString &host, const QString &error)
    {
        emit authMethodsFailed(host, error);
    }

    int provisionCalls = 0;
    int tokenCalls = 0;
    int manualPasswordCalls = 0;
    int discoverCalls = 0;
    QString lastHost;
    QString lastUsername;
    QString lastPassword;
    QString lastToken;
};

struct Notice {
    QString text;
    int duration = 0;
};

// Controller with a single FakeProvider registry and recording
// AddAccountFn / NoticeSink. addAccountResult simulates the
// AccountsController outcome (>= 0 saved, < 0 rejected).
class ProvisioningControllerTest : public ::testing::Test {
protected:
    void SetUp() override { rebuild(); }

    void rebuild(bool withAddAccountFn = true)
    {
        auto registry =
            std::make_unique<compactphone::provisioning::Registry>();
        auto fake = std::make_unique<FakeProvider>();
        provider = fake.get();
        std::vector<std::unique_ptr<compactphone::provisioning::Provider>> ps;
        ps.push_back(std::move(fake));
        registry->setProviders(std::move(ps));

        compactphone::ProvisioningController::AddAccountFn addFn;
        if (withAddAccountFn) {
            addFn = [this](const QVariantMap &params) {
                ++addAccountCalls;
                lastAddParams = params;
                return addAccountResult;
            };
        }
        controller = std::make_unique<compactphone::ProvisioningController>(
            std::move(registry), std::move(addFn),
            [this](const QString &text, int duration) {
                notices.push_back({text, duration});
            });
    }

    FakeProvider *provider = nullptr;
    std::unique_ptr<compactphone::ProvisioningController> controller;
    int addAccountCalls = 0;
    int addAccountResult = 7;
    QVariantMap lastAddParams;
    std::vector<Notice> notices;
};

} // namespace

TEST_F(ProvisioningControllerTest, SuccessfulProvisionAddsAccountAndNotifies)
{
    QSignalSpy provisioned(controller.get(),
                           &compactphone::ProvisioningController::accountProvisioned);
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    controller->provision("fake", "pbx.example.com", "1001", "secret");
    EXPECT_EQ(provider->provisionCalls, 1);
    EXPECT_EQ(provider->lastHost, "pbx.example.com");
    EXPECT_EQ(provider->lastUsername, "1001");
    EXPECT_EQ(provider->lastPassword, "secret");

    QVariantMap params;
    params["username"] = "1001";
    provider->fireSucceeded(params);

    EXPECT_EQ(addAccountCalls, 1);
    EXPECT_EQ(lastAddParams["username"].toString(), QStringLiteral("1001"));
    ASSERT_EQ(provisioned.count(), 1);
    EXPECT_EQ(provisioned[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(provisioned[0][1].toInt(), 7);
    EXPECT_EQ(failed.count(), 0);
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(notices[0].duration, compactphone::notice::kDefault);
}

// Fail closed: when the account cannot be saved, the user must see a failure
// — emitting accountProvisioned anyway would let the wizard report success
// for an account that does not exist.
TEST_F(ProvisioningControllerTest, NegativeAddAccountIdFailsClosed)
{
    addAccountResult = -1;
    QSignalSpy provisioned(controller.get(),
                           &compactphone::ProvisioningController::accountProvisioned);
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    provider->fireSucceeded(QVariantMap{});

    EXPECT_EQ(addAccountCalls, 1);
    EXPECT_EQ(provisioned.count(), 0);
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed[0][0].toString(), QStringLiteral("fake"));
    EXPECT_FALSE(failed[0][1].toString().isEmpty());
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(notices[0].duration, compactphone::notice::kError);
}

// A controller constructed without an AddAccountFn must behave exactly like
// a rejected save, not crash or silently "succeed".
TEST_F(ProvisioningControllerTest, MissingAddAccountFnFailsClosed)
{
    rebuild(/*withAddAccountFn=*/false);
    QSignalSpy provisioned(controller.get(),
                           &compactphone::ProvisioningController::accountProvisioned);
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    provider->fireSucceeded(QVariantMap{});

    EXPECT_EQ(provisioned.count(), 0);
    EXPECT_EQ(failed.count(), 1);
}

TEST_F(ProvisioningControllerTest, UnknownProviderIdFailsProvision)
{
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    controller->provision("nope", "h", "u", "p");

    EXPECT_EQ(provider->provisionCalls, 0);
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed[0][0].toString(), QStringLiteral("nope"));
    EXPECT_TRUE(failed[0][1].toString().contains("nope"));
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(notices[0].duration, compactphone::notice::kError);
}

TEST_F(ProvisioningControllerTest, UnknownProviderIdFailsProvisionWithToken)
{
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    controller->provisionWithToken("nope", "h", "tok");

    EXPECT_EQ(provider->tokenCalls, 0);
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed[0][0].toString(), QStringLiteral("nope"));
    EXPECT_EQ(notices.size(), 1u);
}

TEST_F(ProvisioningControllerTest, UnknownProviderIdFailsManualPassword)
{
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    controller->provideManualPassword("nope", "secret");

    EXPECT_EQ(provider->manualPasswordCalls, 0);
    ASSERT_EQ(failed.count(), 1);
    EXPECT_EQ(failed[0][0].toString(), QStringLiteral("nope"));
    EXPECT_EQ(notices.size(), 1u);
}

// discoverAuthMethods is a wizard probe — failure reports through
// authMethodsFailed (with the host echoed back so the wizard can drop stale
// responses) and must not raise a user-facing notice or provisioningFailed.
TEST_F(ProvisioningControllerTest, UnknownProviderIdFailsDiscoverAuthMethods)
{
    QSignalSpy authFailed(controller.get(),
                          &compactphone::ProvisioningController::authMethodsFailed);
    QSignalSpy failed(controller.get(),
                      &compactphone::ProvisioningController::provisioningFailed);

    controller->discoverAuthMethods("nope", "pbx.example.com");

    EXPECT_EQ(provider->discoverCalls, 0);
    ASSERT_EQ(authFailed.count(), 1);
    EXPECT_EQ(authFailed[0][0].toString(), QStringLiteral("nope"));
    EXPECT_EQ(authFailed[0][1].toString(), QStringLiteral("pbx.example.com"));
    EXPECT_FALSE(authFailed[0][2].toString().isEmpty());
    EXPECT_EQ(failed.count(), 0);
    EXPECT_TRUE(notices.empty());
}

TEST_F(ProvisioningControllerTest, KnownProviderDelegatesEveryEntryPoint)
{
    controller->provisionWithToken("fake", "pbx.example.com", "tok-123");
    EXPECT_EQ(provider->tokenCalls, 1);
    EXPECT_EQ(provider->lastToken, "tok-123");

    controller->provideManualPassword("fake", "typed-by-hand");
    EXPECT_EQ(provider->manualPasswordCalls, 1);
    EXPECT_EQ(provider->lastPassword, "typed-by-hand");

    controller->discoverAuthMethods("fake", "pbx.example.com");
    EXPECT_EQ(provider->discoverCalls, 1);
    EXPECT_EQ(provider->lastHost, "pbx.example.com");
}

// The constructor wires each provider signal to a controller re-emission
// that prepends the provider id, so wizards can tell providers apart.
TEST_F(ProvisioningControllerTest, ProviderSignalsAreReEmittedWithProviderId)
{
    QSignalSpy progressSpy(controller.get(),
                           &compactphone::ProvisioningController::provisioningProgress);
    QSignalSpy failedSpy(controller.get(),
                         &compactphone::ProvisioningController::provisioningFailed);
    QSignalSpy pwSpy(controller.get(),
                     &compactphone::ProvisioningController::passwordRequired);
    QSignalSpy discoveredSpy(controller.get(),
                             &compactphone::ProvisioningController::authMethodsDiscovered);
    QSignalSpy authFailedSpy(controller.get(),
                             &compactphone::ProvisioningController::authMethodsFailed);

    provider->fireProgress("logging-in");
    ASSERT_EQ(progressSpy.count(), 1);
    EXPECT_EQ(progressSpy[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(progressSpy[0][1].toString(), QStringLiteral("logging-in"));

    provider->fireFailed("boom");
    ASSERT_EQ(failedSpy.count(), 1);
    EXPECT_EQ(failedSpy[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(failedSpy[0][1].toString(), QStringLiteral("boom"));
    ASSERT_EQ(notices.size(), 1u);
    EXPECT_EQ(notices[0].duration, compactphone::notice::kError);
    EXPECT_TRUE(notices[0].text.contains("boom"));

    QVariantMap partial;
    partial["username"] = "1001";
    partial["password"] = "";
    provider->firePasswordRequired(partial);
    ASSERT_EQ(pwSpy.count(), 1);
    EXPECT_EQ(pwSpy[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(pwSpy[0][1].toMap()["username"].toString(),
              QStringLiteral("1001"));

    QVariantList methods{QVariantMap{{"id", "password"}}};
    provider->fireAuthMethodsDiscovered("pbx.example.com", methods);
    ASSERT_EQ(discoveredSpy.count(), 1);
    EXPECT_EQ(discoveredSpy[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(discoveredSpy[0][1].toString(), QStringLiteral("pbx.example.com"));
    EXPECT_EQ(discoveredSpy[0][2].toList().size(), 1);

    provider->fireAuthMethodsFailed("pbx.example.com", "unreachable");
    ASSERT_EQ(authFailedSpy.count(), 1);
    EXPECT_EQ(authFailedSpy[0][0].toString(), QStringLiteral("fake"));
    EXPECT_EQ(authFailedSpy[0][2].toString(), QStringLiteral("unreachable"));
}

TEST_F(ProvisioningControllerTest, ProvidersReturnsInjectedDescriptors)
{
    const auto list = controller->providers();
    ASSERT_EQ(list.size(), 1);
    const auto m = list[0].toMap();
    EXPECT_EQ(m["id"].toString(), QStringLiteral("fake"));
    EXPECT_EQ(m["displayName"].toString(), QStringLiteral("Fake"));
}

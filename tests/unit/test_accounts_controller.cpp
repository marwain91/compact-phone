#include <gtest/gtest.h>

#include "core/AccountsController.h"
#include "core/AccountsManager.h"
#include "core/platform/Keychain_memory.h"
#include "models/AccountsModel.h"
#include "persistence/Database.h"

class AccountsControllerTest : public ::testing::Test {
protected:
    compactphone::persistence::Database db;
    compactphone::platform::MemoryKeychain kc;

    void SetUp() override { ASSERT_TRUE(db.openInMemory()); }
};

TEST_F(AccountsControllerTest, AddAccountMapsEveryEditableField)
{
    compactphone::sip::AccountsManager manager(nullptr, &db, &kc);
    compactphone::models::AccountsModel model(&manager);
    compactphone::AccountsController controller(&manager, &model);

    QVariantMap params;
    params["label"] = "Office";
    params["displayName"] = "Jane Agent";
    params["username"] = "1001";
    params["domain"] = "pbx.example.com";
    params["authRealm"] = "office";
    params["proxy"] = "proxy.example.com";
    params["stunServer"] = "stun.example.com";
    params["publicAddress"] = "203.0.113.10";
    params["voicemailNumber"] = "*97";
    params["transport"] = "tls";
    params["registerOnStartup"] = false;
    params["registerIntervalSec"] = 120;
    params["keepaliveIntervalSec"] = 30;
    params["sessionTimersEnabled"] = false;
    params["publishPresenceEnabled"] = true;
    params["iceEnabled"] = true;
    params["hideCallerId"] = true;
    params["srtpMode"] = "required";
    params["allowUntrustedCert"] = true;
    params["dtmfMethod"] = "info";
    params["enabled"] = false;
    params["isDefault"] = true;
    params["sortOrder"] = 7;
    params["password"] = "secret";

    const auto id = controller.addAccount(params);
    ASSERT_NE(id, compactphone::sip::kInvalidAccountId);
    EXPECT_EQ(model.rowCount(), 1);

    const auto snapshot = controller.accountSnapshot(id);
    EXPECT_EQ(snapshot["label"].toString(), QStringLiteral("Office"));
    EXPECT_EQ(snapshot["displayName"].toString(), QStringLiteral("Jane Agent"));
    EXPECT_EQ(snapshot["username"].toString(), QStringLiteral("1001"));
    EXPECT_EQ(snapshot["authUser"].toString(), QStringLiteral("1001"));
    EXPECT_EQ(snapshot["domain"].toString(), QStringLiteral("pbx.example.com"));
    EXPECT_EQ(snapshot["authRealm"].toString(), QStringLiteral("office"));
    EXPECT_EQ(snapshot["proxy"].toString(), QStringLiteral("proxy.example.com"));
    EXPECT_EQ(snapshot["stunServer"].toString(), QStringLiteral("stun.example.com"));
    EXPECT_EQ(snapshot["publicAddress"].toString(), QStringLiteral("203.0.113.10"));
    EXPECT_EQ(snapshot["voicemailNumber"].toString(), QStringLiteral("*97"));
    EXPECT_EQ(snapshot["transport"].toString(), QStringLiteral("tls"));
    EXPECT_FALSE(snapshot["registerOnStartup"].toBool());
    EXPECT_EQ(snapshot["registerIntervalSec"].toInt(), 120);
    EXPECT_EQ(snapshot["keepaliveIntervalSec"].toInt(), 30);
    EXPECT_FALSE(snapshot["sessionTimersEnabled"].toBool());
    EXPECT_TRUE(snapshot["publishPresenceEnabled"].toBool());
    EXPECT_TRUE(snapshot["iceEnabled"].toBool());
    EXPECT_TRUE(snapshot["hideCallerId"].toBool());
    EXPECT_EQ(snapshot["srtpMode"].toString(), QStringLiteral("required"));
    EXPECT_TRUE(snapshot["allowUntrustedCert"].toBool());
    EXPECT_EQ(snapshot["dtmfMethod"].toString(), QStringLiteral("info"));
    EXPECT_FALSE(snapshot["enabled"].toBool());
    EXPECT_TRUE(snapshot["isDefault"].toBool());
    EXPECT_EQ(snapshot["sortOrder"].toInt(), 7);
    EXPECT_EQ(controller.activeAccountId(), -1);
}

TEST_F(AccountsControllerTest, UpdateDefaultEnableAndRemoveRefreshModel)
{
    compactphone::sip::AccountsManager manager(nullptr, &db, &kc);
    compactphone::models::AccountsModel model(&manager);
    compactphone::AccountsController controller(&manager, &model);

    QVariantMap first;
    first["displayName"] = "First";
    first["username"] = "1001";
    first["domain"] = "pbx.example.com";
    first["registerOnStartup"] = false;
    const auto firstId = controller.addAccount(first);
    ASSERT_NE(firstId, compactphone::sip::kInvalidAccountId);

    QVariantMap second;
    second["displayName"] = "Second";
    second["username"] = "1002";
    second["domain"] = "pbx.example.com";
    second["registerOnStartup"] = false;
    const auto secondId = controller.addAccount(second);
    ASSERT_NE(secondId, compactphone::sip::kInvalidAccountId);

    QVariantMap edit;
    edit["displayName"] = "Second Renamed";
    edit["transport"] = "tcp";
    ASSERT_TRUE(controller.updateAccount(secondId, edit));
    EXPECT_EQ(controller.accountSnapshot(secondId)["displayName"].toString(),
              QStringLiteral("Second Renamed"));
    EXPECT_EQ(controller.accountSnapshot(secondId)["transport"].toString(),
              QStringLiteral("tcp"));

    ASSERT_TRUE(controller.setDefaultAccount(secondId));
    EXPECT_TRUE(manager.find(secondId)->isDefault);
    EXPECT_FALSE(manager.find(firstId)->isDefault);

    ASSERT_TRUE(controller.setAccountEnabled(secondId, false));
    EXPECT_FALSE(manager.find(secondId)->enabled);
    EXPECT_EQ(model.rowCount(), 2);

    ASSERT_TRUE(controller.removeAccount(firstId));
    EXPECT_EQ(model.rowCount(), 1);
    EXPECT_FALSE(manager.find(firstId).has_value());
    EXPECT_FALSE(controller.removeAccount(9999));
    EXPECT_TRUE(controller.accountSnapshot(9999).isEmpty());
}

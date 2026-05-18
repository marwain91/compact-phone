#include <gtest/gtest.h>

#include "core/SettingsManager.h"
#include "persistence/Database.h"

class SettingsManagerTest : public ::testing::Test {
protected:
    compactphone::persistence::Database db;

    void SetUp() override { ASSERT_TRUE(db.openInMemory()); }
};

TEST_F(SettingsManagerTest, GetReturnsExistingDefaultSettings)
{
    compactphone::sip::SettingsManager settings(&db);

    EXPECT_EQ(settings.getOr("log_level", "missing"), "info");
    EXPECT_EQ(settings.getOr("ringtone_enabled", "0"), "1");
    EXPECT_EQ(settings.getOr("missing", "fallback"), "fallback");
    EXPECT_FALSE(settings.get("missing").has_value());
}

TEST_F(SettingsManagerTest, SetInsertsAndOverwritesValues)
{
    compactphone::sip::SettingsManager settings(&db);

    ASSERT_TRUE(settings.set("theme_id", "dark"));
    EXPECT_EQ(settings.getOr("theme_id", ""), "dark");

    ASSERT_TRUE(settings.set("theme_id", "light"));
    auto theme = settings.get("theme_id");
    ASSERT_TRUE(theme.has_value());
    EXPECT_EQ(*theme, "light");
}

TEST_F(SettingsManagerTest, InstancesSharePersistedValues)
{
    compactphone::sip::SettingsManager first(&db);
    ASSERT_TRUE(first.set("capture_device_id", "12"));

    compactphone::sip::SettingsManager second(&db);
    EXPECT_EQ(second.getOr("capture_device_id", ""), "12");
}

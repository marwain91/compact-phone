#include <gtest/gtest.h>

#include "core/platform/Autostart_memory.h"

using compactphone::platform::MemoryAutostart;

TEST(MemoryAutostartTest, DefaultsDisabledAndSupported)
{
    MemoryAutostart a;
    EXPECT_TRUE(a.isSupported());
    EXPECT_FALSE(a.isEnabled());
}

TEST(MemoryAutostartTest, SetEnabledRoundTrips)
{
    MemoryAutostart a;
    EXPECT_TRUE(a.setEnabled(true));
    EXPECT_TRUE(a.isEnabled());
    EXPECT_TRUE(a.setEnabled(false));
    EXPECT_FALSE(a.isEnabled());
}

TEST(MemoryAutostartTest, SimulatedFailureDoesNotChangeState)
{
    MemoryAutostart a;
    a.failNextSetEnabled();
    EXPECT_FALSE(a.setEnabled(true));   // failure reported
    EXPECT_FALSE(a.isEnabled());        // state unchanged
    EXPECT_TRUE(a.setEnabled(true));    // next call succeeds
    EXPECT_TRUE(a.isEnabled());
}

TEST(MemoryAutostartTest, SupportedFlagControllable)
{
    MemoryAutostart a;
    a.setSupported(false);
    EXPECT_FALSE(a.isSupported());
}

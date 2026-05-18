#include <gtest/gtest.h>

#include "core/SipEngine.h"

TEST(SipEngineLifecycle, StartsAndStops)
{
    compactphone::sip::SipEngine engine;
    ASSERT_TRUE(engine.start(/*sipPort=*/0));
    EXPECT_TRUE(engine.isRunning());
    engine.stop();
    EXPECT_FALSE(engine.isRunning());
}

TEST(SipEngineLifecycle, RestartAfterStop)
{
    compactphone::sip::SipEngine engine;
    ASSERT_TRUE(engine.start(0));
    engine.stop();
    ASSERT_TRUE(engine.start(0));
    engine.stop();
    SUCCEED();
}

TEST(SipEngineLifecycle, DoubleStartIsNoop)
{
    compactphone::sip::SipEngine engine;
    ASSERT_TRUE(engine.start(0));
    ASSERT_TRUE(engine.start(0));
    engine.stop();
}

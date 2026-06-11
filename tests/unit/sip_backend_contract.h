#pragma once

// Backend-agnostic ISipBackend contract. Every backend (fake, pjsip,
// baresip) must pass this suite unchanged — instantiate it with:
//
//   INSTANTIATE_TEST_SUITE_P(MyBackend, SipBackendContract,
//       ::testing::Values(BackendFactory{[] { return std::make_unique<...>(); }}));
//
// Tests here must not depend on a remote peer or on fake-only scripting;
// they assert lifecycle, argument validation, and id-lifetime rules.
// Include this header from exactly one TU per test binary (TEST_P emits
// definitions); co-resident backends share that TU with multiple
// INSTANTIATE calls.

#include "core/sipbackend/ISipBackend.h"

#include <gtest/gtest.h>

#include <functional>
#include <memory>

namespace compactphone::sipbackend::testing {

using BackendFactory = std::function<std::unique_ptr<ISipBackend>()>;

class SipBackendContract
    : public ::testing::TestWithParam<BackendFactory> {
protected:
    void SetUp() override { backend = GetParam()(); }
    void TearDown() override
    {
        if (backend && backend->isRunning())
            backend->stop();
    }

    std::unique_ptr<ISipBackend> backend;
};

inline AccountSettings contractAccount()
{
    AccountSettings a;
    a.username = "contract";
    a.domain = "contract.test";
    a.password = "pw";
    return a;
}

TEST_P(SipBackendContract, StartsAndStops)
{
    EXPECT_FALSE(backend->isRunning());
    EXPECT_TRUE(backend->start(EngineConfig{}));
    EXPECT_TRUE(backend->isRunning());
    backend->stop();
    EXPECT_FALSE(backend->isRunning());
}

TEST_P(SipBackendContract, StopIsIdempotent)
{
    backend->start(EngineConfig{});
    backend->stop();
    backend->stop();
    EXPECT_FALSE(backend->isRunning());
}

TEST_P(SipBackendContract, AccountOpsRequireRunningEngine)
{
    EXPECT_EQ(backend->addAccount(contractAccount()), kInvalidAccountId);
}

TEST_P(SipBackendContract, CallOpsOnUnknownIdsAreSafeAndFalse)
{
    backend->start(EngineConfig{});
    const CallId bogus = 9999;
    EXPECT_FALSE(backend->answer(bogus));
    EXPECT_FALSE(backend->decline(bogus, 486));
    EXPECT_FALSE(backend->hold(bogus));
    EXPECT_FALSE(backend->unhold(bogus));
    EXPECT_FALSE(backend->setMuted(bogus, true));
    EXPECT_FALSE(backend->sendDtmf(bogus, "1", DtmfMethod::Rfc2833));
    EXPECT_FALSE(backend->blindTransfer(bogus, "sip:x@y"));
    EXPECT_FALSE(backend->redirect(bogus, "sip:x@y"));
    EXPECT_FALSE(backend->attendedTransfer(bogus, bogus));
    EXPECT_FALSE(backend->bridge(bogus, bogus));
    EXPECT_FALSE(backend->startRecording(bogus, "/tmp/x.wav"));
    EXPECT_FALSE(backend->stopRecording(bogus));
    EXPECT_FALSE(backend->playFile(bogus, "/tmp/x.wav", false));
    EXPECT_FALSE(backend->stopFile(bogus));
    EXPECT_FALSE(backend->isMediaActive(bogus));
    EXPECT_FALSE(backend->isCaptureTransmitting(bogus));
    backend->hangup(bogus);       // must not crash
    backend->releaseCall(bogus);  // must not crash
    StreamStats s = backend->streamStats(bogus);
    EXPECT_DOUBLE_EQ(s.mos, -1.0);
    EXPECT_DOUBLE_EQ(s.lossPct, -1.0);
    EXPECT_EQ(s.rttMs, -1);
    EXPECT_EQ(s.jitterMs, -1);
}

TEST_P(SipBackendContract, MakeCallRequiresKnownAccount)
{
    backend->start(EngineConfig{});
    EXPECT_EQ(backend->makeCall(kInvalidAccountId, "sip:x@y"),
              kInvalidCallId);
    EXPECT_EQ(backend->makeCall(424242, "sip:x@y"), kInvalidCallId);
}

TEST_P(SipBackendContract, AccountIdsAreUniqueAndRemovable)
{
    backend->start(EngineConfig{});
    const auto a = backend->addAccount(contractAccount());
    const auto b = backend->addAccount(contractAccount());
    ASSERT_NE(a, kInvalidAccountId);
    ASSERT_NE(b, kInvalidAccountId);
    EXPECT_NE(a, b);
    EXPECT_TRUE(backend->removeAccount(a));
    EXPECT_FALSE(backend->removeAccount(a));  // already gone
}

TEST_P(SipBackendContract, WatchRequiresKnownAccount)
{
    backend->start(EngineConfig{});
    EXPECT_EQ(backend->watch(424242, "sip:line@x"), kInvalidWatchId);
    const auto acc = backend->addAccount(contractAccount());
    const auto w = backend->watch(acc, "sip:line@x");
    ASSERT_NE(w, kInvalidWatchId);
    EXPECT_TRUE(backend->unwatch(w));
    EXPECT_FALSE(backend->unwatch(w));
}

TEST_P(SipBackendContract, StopWithoutStartIsSafe)
{
    backend->stop();   // never started: must not crash
    EXPECT_FALSE(backend->isRunning());
}

TEST_P(SipBackendContract, MakeCallRequiresRunningEngine)
{
    EXPECT_EQ(backend->makeCall(1, "sip:x@y"), kInvalidCallId);
}

TEST_P(SipBackendContract, StopDropsAllState)
{
    // ISipBackend stop(): "drops all accounts, calls, and watches — a
    // restarted backend starts empty."
    backend->start(EngineConfig{});
    const auto acc = backend->addAccount(contractAccount());
    ASSERT_NE(acc, kInvalidAccountId);
    backend->stop();
    backend->start(EngineConfig{});
    EXPECT_FALSE(backend->removeAccount(acc));
    EXPECT_EQ(backend->watch(acc, "sip:line@x"), kInvalidWatchId);
    EXPECT_EQ(backend->makeCall(acc, "sip:x@y"), kInvalidCallId);
}

TEST_P(SipBackendContract, AccountOpsAfterStopAreRefused)
{
    backend->start(EngineConfig{});
    backend->stop();
    EXPECT_EQ(backend->addAccount(contractAccount()), kInvalidAccountId);
}

TEST_P(SipBackendContract, WatchOnRemovedAccountIsRefused)
{
    backend->start(EngineConfig{});
    const auto acc = backend->addAccount(contractAccount());
    ASSERT_TRUE(backend->removeAccount(acc));
    EXPECT_EQ(backend->watch(acc, "sip:line@x"), kInvalidWatchId);
    EXPECT_FALSE(backend->sendMessage(acc, "sip:x@y", "hi"));
}

} // namespace compactphone::sipbackend::testing

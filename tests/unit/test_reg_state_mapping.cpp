#include <gtest/gtest.h>

#include "core/AccountsManager.h"

// mapRegEvent is the pure extraction of AccountImpl::onRegState's decision
// logic: (pj regIsActive flag, SIP status code, reason, previous lastError)
// -> (RegistrationState, next lastError). A mis-mapping here means accounts
// silently never re-register or the UI shows Registered while SIP is down —
// previously this logic was only reachable through a live PJSIP
// registration, so none of it was unit-tested.

using compactphone::sip::mapRegEvent;
using compactphone::sip::RegError;
using compactphone::sip::RegistrationState;

namespace {
const RegError kNoError{};
const RegError kStaleError{403, "Forbidden"};
} // namespace

TEST(RegStateMapping, ActiveWith200IsRegistered)
{
    const auto upd = mapRegEvent(true, 200, "OK", kNoError);
    EXPECT_EQ(upd.state, RegistrationState::Registered);
    EXPECT_TRUE(upd.error.empty());
}

TEST(RegStateMapping, AnyActive2xxIsRegistered)
{
    const auto upd = mapRegEvent(true, 202, "Accepted", kNoError);
    EXPECT_EQ(upd.state, RegistrationState::Registered);
    EXPECT_TRUE(upd.error.empty());
}

TEST(RegStateMapping, SuccessClearsPreviousError)
{
    // Recovery after a failure: a fresh 2xx must wipe the stale error so
    // lastRegErrorOf() doesn't keep reporting a failure that's been fixed.
    const auto upd = mapRegEvent(true, 200, "OK", kStaleError);
    EXPECT_EQ(upd.state, RegistrationState::Registered);
    EXPECT_TRUE(upd.error.empty());
}

TEST(RegStateMapping, CodeZeroIsRegisteringRegardlessOfActiveFlag)
{
    // code 0 = REGISTER sent, no final response yet (PJSIP progress event).
    // The active flag reflects the *previous* binding and must not short-
    // circuit the in-flight indication.
    EXPECT_EQ(mapRegEvent(false, 0, "", kNoError).state,
              RegistrationState::Registering);
    EXPECT_EQ(mapRegEvent(true, 0, "", kNoError).state,
              RegistrationState::Registering);
}

TEST(RegStateMapping, RegisteringPreservesPreviousError)
{
    // While a retry is in flight the user must still be able to read why
    // the last attempt failed.
    const auto upd = mapRegEvent(false, 0, "", kStaleError);
    EXPECT_EQ(upd.state, RegistrationState::Registering);
    EXPECT_EQ(upd.error.code, kStaleError.code);
    EXPECT_EQ(upd.error.reason, kStaleError.reason);
}

TEST(RegStateMapping, Inactive2xxIsConfirmedUnregister)
{
    const auto upd = mapRegEvent(false, 200, "OK", kNoError);
    EXPECT_EQ(upd.state, RegistrationState::Unregistered);
    EXPECT_TRUE(upd.error.empty());
}

TEST(RegStateMapping, UnregisteredPreservesPreviousError)
{
    const auto upd = mapRegEvent(false, 200, "OK", kStaleError);
    EXPECT_EQ(upd.state, RegistrationState::Unregistered);
    EXPECT_EQ(upd.error.code, kStaleError.code);
    EXPECT_EQ(upd.error.reason, kStaleError.reason);
}

TEST(RegStateMapping, FinalNon2xxIsFailedAndStoresCodeAndReason)
{
    for (const int code : {401, 403, 404, 408, 423, 500, 503}) {
        const auto upd = mapRegEvent(false, code, "why", kNoError);
        EXPECT_EQ(upd.state, RegistrationState::Failed) << "code " << code;
        EXPECT_EQ(upd.error.code, code);
        EXPECT_EQ(upd.error.reason, "why");
    }
}

TEST(RegStateMapping, RefreshFailureWhileBindingStillActiveIsFailed)
{
    // A non-2xx on refresh can arrive while regIsActive still reports the
    // old binding as live (e.g. credentials revoked mid-session). Showing
    // Registered here is exactly the "UI says registered while SIP is
    // down" bug this mapping guards against.
    const auto upd = mapRegEvent(true, 401, "Unauthorized", kNoError);
    EXPECT_EQ(upd.state, RegistrationState::Failed);
    EXPECT_EQ(upd.error.code, 401);
    EXPECT_EQ(upd.error.reason, "Unauthorized");
}

TEST(RegStateMapping, FailureOverwritesPreviousError)
{
    const auto upd = mapRegEvent(false, 408, "Request Timeout", kStaleError);
    EXPECT_EQ(upd.state, RegistrationState::Failed);
    EXPECT_EQ(upd.error.code, 408);
    EXPECT_EQ(upd.error.reason, "Request Timeout");
}

TEST(RegStateMapping, ProvisionalNon2xxFinalCodesAreFailed)
{
    // 1xx/3xx land in the catch-all Failed branch — they are not success
    // and not "in flight" (PJSIP signals in-flight with code 0).
    EXPECT_EQ(mapRegEvent(false, 100, "Trying", kNoError).state,
              RegistrationState::Failed);
    EXPECT_EQ(mapRegEvent(false, 302, "Moved", kNoError).state,
              RegistrationState::Failed);
}

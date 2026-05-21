#pragma once

#include "Keychain.h"

#include <memory>

namespace compactphone::platform {

// Wraps another IKeychain so every get() must be preceded by a successful
// LocalAuthentication biometric / device-passcode prompt. The LAContext is
// retained across get() calls and its
// touchIDAuthenticationAllowableReuseDuration is set to the auth-cache
// window, so back-to-back reads (e.g. PhoneController booting multiple
// accounts) only prompt once. set() and erase() are passed through
// without a prompt — writes are always allowed when the calling process
// already has the password in hand.
std::unique_ptr<IKeychain> wrapWithTouchId(std::unique_ptr<IKeychain> inner);

} // namespace compactphone::platform

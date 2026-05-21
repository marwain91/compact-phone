#include "TouchIdGate.h"

#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>

#include <spdlog/spdlog.h>

#include <dispatch/dispatch.h>

namespace compactphone::platform {

namespace {

class TouchIdGatedKeychain final : public IKeychain {
public:
    explicit TouchIdGatedKeychain(std::unique_ptr<IKeychain> inner)
        : m_inner(std::move(inner))
    {
        m_ctx = [[LAContext alloc] init];
        // Keep a Touch ID approval valid for five minutes so re-register
        // bursts (network back / wake / account-enable toggle) don't keep
        // re-prompting in the same session.
        m_ctx.touchIDAuthenticationAllowableReuseDuration = 5 * 60;
    }

    std::optional<std::string> get(const std::string &ref) override
    {
        if (!authenticate()) return std::nullopt;
        return m_inner->get(ref);
    }

    bool set(const std::string &ref, const std::string &password) override
    {
        return m_inner->set(ref, password);
    }

    bool erase(const std::string &ref) override
    {
        return m_inner->erase(ref);
    }

private:
    bool authenticate()
    {
        @autoreleasepool {
            NSError *evalErr = nil;
            // Falls back through Touch ID -> Apple Watch unlock ->
            // device passcode automatically, so the prompt works on Macs
            // without a fingerprint sensor too.
            const LAPolicy policy = LAPolicyDeviceOwnerAuthentication;
            if (![m_ctx canEvaluatePolicy:policy error:&evalErr]) {
                // No biometric and no passcode set — let the call through
                // so a misconfigured Mac doesn't lock the user out of
                // their own credentials.
                spdlog::warn("TouchIdGate: canEvaluatePolicy failed ({}); "
                             "allowing without biometric",
                             evalErr ? evalErr.localizedDescription.UTF8String
                                     : "unknown");
                return true;
            }

            __block bool authorized = false;
            dispatch_semaphore_t sema = dispatch_semaphore_create(0);
            [m_ctx evaluatePolicy:policy
                  localizedReason:@"unlock your SIP account password"
                            reply:^(BOOL ok, NSError * _Nullable err) {
                authorized = ok;
                if (!ok) {
                    spdlog::warn("TouchIdGate: evaluatePolicy denied ({})",
                                 err ? err.localizedDescription.UTF8String
                                     : "user canceled");
                }
                dispatch_semaphore_signal(sema);
            }];
            // Blocks the calling thread until the user resolves the system
            // biometric dialog. The dialog is rendered by an out-of-process
            // helper (`LocalAuthenticationRemoteService`), so blocking our
            // main thread doesn't stall the prompt's UI.
            dispatch_semaphore_wait(sema, DISPATCH_TIME_FOREVER);
            return authorized;
        }
    }

    std::unique_ptr<IKeychain> m_inner;
    LAContext *m_ctx;
};

} // namespace

std::unique_ptr<IKeychain> wrapWithTouchId(std::unique_ptr<IKeychain> inner)
{
    return std::make_unique<TouchIdGatedKeychain>(std::move(inner));
}

} // namespace compactphone::platform

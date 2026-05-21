#include "Keychain_macos.h"

#import <Security/Security.h>
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>

#include <spdlog/spdlog.h>

#include <atomic>

namespace compactphone::platform {

namespace {

// Marker stored in kSecAttrComment so we can tell migrated items apart from
// legacy ones without a trial decryption. Bump the suffix if the ACL recipe
// changes and items need to be rewritten again.
NSString *const kMigrationMarker = @"v2-userpresence";

// Sentinel account name used by the Touch ID feasibility probe. Lives in
// the same kSecAttrService bucket so it shares the app's ACL group.
const char kProbeAccount[] = "__cp_acl_probe__";

NSDictionary *baseQuery(const std::string &ref)
{
    NSString *account = [NSString stringWithUTF8String:ref.c_str()];
    return @{
        (id)kSecClass:       (id)kSecClassGenericPassword,
        (id)kSecAttrService: @"eu.havliczech.compactphone",
        (id)kSecAttrAccount: account
    };
}

SecAccessControlRef makeUserPresenceAccessControl()
{
    CFErrorRef err = NULL;
    SecAccessControlRef ac = SecAccessControlCreateWithFlags(
        kCFAllocatorDefault,
        kSecAttrAccessibleWhenUnlockedThisDeviceOnly,
        kSecAccessControlUserPresence,
        &err);
    if (!ac && err) {
        NSError *nsErr = (__bridge_transfer NSError *)err;
        spdlog::error("SecAccessControlCreateWithFlags failed: {}",
                      nsErr.localizedDescription.UTF8String);
    } else if (err) {
        CFRelease(err);
    }
    return ac;
}

OSStatus addWithBiometricACL(const std::string &ref, NSData *data)
{
    SecAccessControlRef ac = makeUserPresenceAccessControl();
    NSMutableDictionary *add = [baseQuery(ref) mutableCopy];
    add[(id)kSecValueData]    = data;
    add[(id)kSecAttrComment]  = kMigrationMarker;
    if (ac) {
        add[(id)kSecAttrAccessControl] = (__bridge_transfer id)ac;
    } else {
        add[(id)kSecAttrAccessible] =
            (id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
    }
    return SecItemAdd((__bridge CFDictionaryRef)add, NULL);
}

OSStatus addLegacy(const std::string &ref, NSData *data)
{
    NSMutableDictionary *add = [baseQuery(ref) mutableCopy];
    add[(id)kSecValueData] = data;
    add[(id)kSecAttrAccessible] =
        (id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
    return SecItemAdd((__bridge CFDictionaryRef)add, NULL);
}

// Determines whether this binary can write keychain items with the user-
// presence access control. Ad-hoc-signed dev builds fail with
// errSecMissingEntitlement (-34018) because Touch ID-protected items
// require the Keychain Sharing entitlement / a stable signing identity.
// Probed once per process and cached.
bool touchIdItemsSupported()
{
    static std::atomic<int> cached{-1}; // -1 = unknown, 0 = no, 1 = yes
    int v = cached.load(std::memory_order_relaxed);
    if (v != -1) return v == 1;

    NSData *probeData = [@"probe" dataUsingEncoding:NSUTF8StringEncoding];
    // Make sure the probe slot is empty before attempting the test add.
    SecItemDelete((__bridge CFDictionaryRef)baseQuery(kProbeAccount));

    OSStatus rc = addWithBiometricACL(kProbeAccount, probeData);
    if (rc == errSecSuccess) {
        SecItemDelete((__bridge CFDictionaryRef)baseQuery(kProbeAccount));
        cached.store(1, std::memory_order_relaxed);
        spdlog::info("MacKeychain: Touch ID-protected items supported");
        return true;
    }
    cached.store(0, std::memory_order_relaxed);
    spdlog::warn("MacKeychain: Touch ID-protected items NOT supported "
                 "(probe rc={}); credentials will use the legacy ACL — "
                 "this is normal for ad-hoc-signed dev builds",
                 static_cast<long>(rc));
    return false;
}

} // namespace

MacKeychain::MacKeychain() = default;
MacKeychain::~MacKeychain() = default;

std::optional<std::string> MacKeychain::get(const std::string &ref)
{
    @autoreleasepool {
        NSMutableDictionary *q = [baseQuery(ref) mutableCopy];
        q[(id)kSecReturnData]        = (id)kCFBooleanTrue;
        q[(id)kSecReturnAttributes]  = (id)kCFBooleanTrue;
        q[(id)kSecMatchLimit]        = (id)kSecMatchLimitOne;

        CFTypeRef result = NULL;
        OSStatus rc = SecItemCopyMatching((__bridge CFDictionaryRef)q, &result);
        if (rc == errSecUserCanceled || rc == errSecAuthFailed) {
            return std::nullopt;
        }
        if (rc != errSecSuccess || !result) return std::nullopt;

        NSDictionary *attrs = (__bridge_transfer NSDictionary *)result;
        NSData *data = attrs[(id)kSecValueData];
        if (!data) return std::nullopt;
        std::string plaintext(reinterpret_cast<const char *>(data.bytes),
                              data.length);

        NSString *comment = attrs[(id)kSecAttrComment];
        const bool alreadyMigrated =
            [comment isEqualToString:kMigrationMarker];
        if (alreadyMigrated) return plaintext;

        // Migration path. We have a legacy item and want it under the
        // user-presence ACL. Before doing the destructive delete+re-add,
        // confirm the environment will accept the new ACL — otherwise
        // we'd delete the password and fail to put it back.
        if (!touchIdItemsSupported()) {
            return plaintext;
        }

        OSStatus drc = SecItemDelete((__bridge CFDictionaryRef)baseQuery(ref));
        if (drc != errSecSuccess && drc != errSecItemNotFound) {
            spdlog::warn("MacKeychain::get: migration delete failed rc={}; "
                         "skipping migration",
                         static_cast<long>(drc));
            return plaintext;
        }

        NSData *rawData = [NSData dataWithBytes:plaintext.data()
                                         length:plaintext.size()];
        OSStatus arc = addWithBiometricACL(ref, rawData);
        if (arc == errSecSuccess) {
            spdlog::info("MacKeychain::get: migrated {} to Touch ID ACL", ref);
            return plaintext;
        }

        // Restore the legacy item so we don't lose credentials.
        spdlog::warn("MacKeychain::get: migration add failed rc={}, restoring "
                     "legacy item",
                     static_cast<long>(arc));
        OSStatus restoreRc = addLegacy(ref, rawData);
        if (restoreRc != errSecSuccess) {
            spdlog::error("MacKeychain::get: legacy restore failed rc={}; "
                          "credentials lost from keychain, in-memory copy "
                          "still valid this session",
                          static_cast<long>(restoreRc));
        }
        return plaintext;
    }
}

bool MacKeychain::set(const std::string &ref, const std::string &password)
{
    @autoreleasepool {
        NSData *data = [NSData dataWithBytes:password.data()
                                      length:password.size()];
        // ACL is fixed at add-time, so a delete + add is the only way to
        // change the access control on an existing item.
        SecItemDelete((__bridge CFDictionaryRef)baseQuery(ref));

        OSStatus rc;
        if (touchIdItemsSupported()) {
            rc = addWithBiometricACL(ref, data);
        } else {
            rc = addLegacy(ref, data);
        }
        if (rc != errSecSuccess) {
            spdlog::error("MacKeychain::set: SecItemAdd failed: {}",
                          static_cast<long>(rc));
            return false;
        }
        return true;
    }
}

bool MacKeychain::erase(const std::string &ref)
{
    OSStatus rc = SecItemDelete((__bridge CFDictionaryRef)baseQuery(ref));
    return rc == errSecSuccess || rc == errSecItemNotFound;
}

} // namespace compactphone::platform

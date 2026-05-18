#include "Keychain_macos.h"

#import <Security/Security.h>
#import <Foundation/Foundation.h>

namespace compactphone::platform {

namespace {

NSDictionary *baseQuery(const std::string &ref)
{
    NSString *account = [NSString stringWithUTF8String:ref.c_str()];
    return @{
        (id)kSecClass:       (id)kSecClassGenericPassword,
        (id)kSecAttrService: @"eu.havliczech.compactphone",
        (id)kSecAttrAccount: account
    };
}

} // namespace

MacKeychain::MacKeychain() = default;
MacKeychain::~MacKeychain() = default;

std::optional<std::string> MacKeychain::get(const std::string &ref)
{
    NSMutableDictionary *q = [baseQuery(ref) mutableCopy];
    q[(id)kSecReturnData]  = (id)kCFBooleanTrue;
    q[(id)kSecMatchLimit]  = (id)kSecMatchLimitOne;

    CFTypeRef result = NULL;
    OSStatus rc = SecItemCopyMatching((__bridge CFDictionaryRef)q, &result);
    if (rc != errSecSuccess || !result) return std::nullopt;

    NSData *data = (__bridge_transfer NSData *)result;
    return std::string(reinterpret_cast<const char *>(data.bytes), data.length);
}

bool MacKeychain::set(const std::string &ref, const std::string &password)
{
    NSData *data = [NSData dataWithBytes:password.data() length:password.size()];
    NSDictionary *q = baseQuery(ref);

    // Try update first.
    NSDictionary *updateAttrs = @{ (id)kSecValueData: data };
    OSStatus rc = SecItemUpdate((__bridge CFDictionaryRef)q,
                                (__bridge CFDictionaryRef)updateAttrs);
    if (rc == errSecSuccess) return true;
    if (rc != errSecItemNotFound) return false;

    // Doesn't exist yet — add a fresh entry.
    NSMutableDictionary *add = [q mutableCopy];
    add[(id)kSecValueData] = data;
    add[(id)kSecAttrAccessible] =
        (id)kSecAttrAccessibleWhenUnlockedThisDeviceOnly;
    rc = SecItemAdd((__bridge CFDictionaryRef)add, NULL);
    return rc == errSecSuccess;
}

bool MacKeychain::erase(const std::string &ref)
{
    OSStatus rc = SecItemDelete((__bridge CFDictionaryRef)baseQuery(ref));
    return rc == errSecSuccess || rc == errSecItemNotFound;
}

} // namespace compactphone::platform

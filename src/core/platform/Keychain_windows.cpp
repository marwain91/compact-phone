#include "Keychain_windows.h"

#ifdef _WIN32

#include <windows.h>
#include <wincred.h>

#include <string>
#include <vector>

#pragma comment(lib, "advapi32.lib")

namespace compactphone::platform {

namespace {

std::wstring targetName(const std::string &ref)
{
    const std::string prefix = "eu.havliczech.compactphone:" + ref;
    const int wlen = MultiByteToWideChar(
        CP_UTF8, 0, prefix.c_str(), -1, nullptr, 0);
    std::wstring out(wlen, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, prefix.c_str(), -1, out.data(), wlen);
    if (!out.empty() && out.back() == L'\0') out.pop_back();
    return out;
}

} // namespace

WindowsKeychain::WindowsKeychain() = default;
WindowsKeychain::~WindowsKeychain() = default;

std::optional<std::string> WindowsKeychain::get(const std::string &ref)
{
    PCREDENTIALW cred = nullptr;
    if (!CredReadW(targetName(ref).c_str(),
                   CRED_TYPE_GENERIC, 0, &cred) || !cred) {
        return std::nullopt;
    }
    std::string out(reinterpret_cast<const char *>(cred->CredentialBlob),
                    cred->CredentialBlobSize);
    CredFree(cred);
    return out;
}

bool WindowsKeychain::set(const std::string &ref, const std::string &password)
{
    std::wstring target = targetName(ref);
    CREDENTIALW c = {};
    c.Type = CRED_TYPE_GENERIC;
    c.TargetName = target.data();
    c.CredentialBlobSize = static_cast<DWORD>(password.size());
    c.CredentialBlob = reinterpret_cast<LPBYTE>(
        const_cast<char *>(password.data()));
    c.Persist = CRED_PERSIST_LOCAL_MACHINE;
    return CredWriteW(&c, 0) == TRUE;
}

bool WindowsKeychain::erase(const std::string &ref)
{
    return CredDeleteW(targetName(ref).c_str(),
                       CRED_TYPE_GENERIC, 0) == TRUE
        || GetLastError() == ERROR_NOT_FOUND;
}

} // namespace compactphone::platform

#endif // _WIN32

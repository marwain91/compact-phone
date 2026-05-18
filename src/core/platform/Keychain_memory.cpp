#include "Keychain_memory.h"

namespace compactphone::platform {

std::optional<std::string> MemoryKeychain::get(const std::string &ref)
{
    auto it = m_store.find(ref);
    if (it == m_store.end()) return std::nullopt;
    return it->second;
}

bool MemoryKeychain::set(const std::string &ref, const std::string &password)
{
    m_store[ref] = password;
    return true;
}

bool MemoryKeychain::erase(const std::string &ref)
{
    return m_store.erase(ref) > 0;
}

} // namespace compactphone::platform

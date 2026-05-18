#include "Registry.h"

#include "DaktelaProvider.h"

#include <QVariantMap>

namespace compactphone::provisioning {

Registry::Registry()
{
    // Built-in providers. Append new ones here (3CX, FreeSWITCH, Twilio, …).
    m_providers.emplace_back(std::make_unique<DaktelaProvider>());
}

Registry::~Registry() = default;

void Registry::setProviders(std::vector<std::unique_ptr<Provider>> providers)
{
    m_providers = std::move(providers);
}

Provider *Registry::find(const QString &id) const
{
    for (const auto &p : m_providers) {
        if (p->id() == id) return p.get();
    }
    return nullptr;
}

QStringList Registry::ids() const
{
    QStringList out;
    out.reserve(static_cast<int>(m_providers.size()));
    for (const auto &p : m_providers) out << p->id();
    return out;
}

QVariantList Registry::descriptors() const
{
    QVariantList out;
    out.reserve(static_cast<int>(m_providers.size()));
    for (const auto &p : m_providers) {
        QVariantMap m;
        m[QStringLiteral("id")] = p->id();
        m[QStringLiteral("displayName")] = p->displayName();
        m[QStringLiteral("hostPlaceholder")] = p->hostPlaceholder();
        m[QStringLiteral("markPath")] = p->markPath();
        m[QStringLiteral("markPathDark")] = p->markPathDark();
        out.push_back(m);
    }
    return out;
}

} // namespace compactphone::provisioning

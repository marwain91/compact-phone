#pragma once

#include "Provider.h"

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVariantList>

#include <memory>
#include <vector>

namespace compactphone::provisioning {

// Holds the set of provisioning providers built into this build. PhoneController
// owns one Registry and exposes its providers to QML. Tests can construct
// their own Registry with a stubbed provider instead of the built-ins.
class Registry {
public:
    Registry();
    ~Registry();

    // Replace the default providers with a hand-built list (tests only).
    void setProviders(std::vector<std::unique_ptr<Provider>> providers);

    // Provider lookup. Returns nullptr if unknown.
    Provider *find(const QString &id) const;

    // List of {"id": "...", "displayName": "...", "hostPlaceholder": "..."}
    // entries suitable for binding directly to a QML model.
    QVariantList descriptors() const;

    // Just the ids, in registration order.
    QStringList ids() const;

private:
    std::vector<std::unique_ptr<Provider>> m_providers;
};

} // namespace compactphone::provisioning

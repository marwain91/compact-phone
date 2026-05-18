#pragma once

#include "Contact.h"

#include <functional>
#include <optional>
#include <vector>

namespace compactphone::persistence { class Database; }

namespace compactphone::sip {

class ContactsManager {
public:
    explicit ContactsManager(persistence::Database *db);

    ContactsManager(const ContactsManager &) = delete;
    ContactsManager &operator=(const ContactsManager &) = delete;

    ContactId add(const Contact &c);
    bool update(const Contact &c);
    bool remove(ContactId id);

    std::vector<Contact> list() const;
    std::optional<Contact> findById(ContactId id) const;
    std::optional<Contact> findByUri(const std::string &sipUri) const;

private:
    persistence::Database *m_db;
};

} // namespace compactphone::sip

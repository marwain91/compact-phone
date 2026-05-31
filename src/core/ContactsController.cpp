#include "ContactsController.h"

#include "Contact.h"
#include "ContactImporter.h"
#include "ContactsManager.h"
#include "models/ContactsModel.h"

#include <spdlog/spdlog.h>

#include <QFile>

namespace compactphone {

ContactsController::ContactsController(sip::ContactsManager *contacts,
                                      models::ContactsModel *model,
                                      QObject *parent)
    : QObject(parent), m_contacts(contacts), m_model(model) {}

ContactsController::~ContactsController() = default;

QAbstractListModel *ContactsController::model() const { return m_model; }

int ContactsController::add(const QString &displayName, const QString &sipUri,
                            const QString &phone)
{
    if (!m_contacts) return sip::kInvalidContactId;
    sip::Contact c;
    c.displayName = displayName.toStdString();
    c.sipUri = sipUri.toStdString();
    c.phone = phone.toStdString();
    const auto id = m_contacts->add(c);
    if (m_model) m_model->refresh();
    return id;
}

bool ContactsController::update(int contactId, const QString &displayName,
                                const QString &sipUri, const QString &phone)
{
    if (!m_contacts) return false;
    auto cur = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!cur) return false;
    cur->displayName = displayName.toStdString();
    cur->sipUri = sipUri.toStdString();
    cur->phone = phone.toStdString();
    const bool ok = m_contacts->update(*cur);
    if (ok && m_model) m_model->refresh();
    return ok;
}

bool ContactsController::remove(int contactId)
{
    if (!m_contacts) return false;
    const bool ok = m_contacts->remove(static_cast<sip::ContactId>(contactId));
    if (ok && m_model) m_model->refresh();
    return ok;
}

bool ContactsController::setFavorite(int contactId, bool favorite)
{
    if (!m_contacts) return false;
    auto cur = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!cur) return false;
    if (cur->favorite == favorite) return true;
    cur->favorite = favorite;
    const bool ok = m_contacts->update(*cur);
    if (ok && m_model) m_model->refresh();
    return ok;
}

int ContactsController::importFromFile(const QString &path)
{
    if (!m_contacts) return 0;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        spdlog::warn("ContactsController::importFromFile: cannot open {}: {}",
                     path.toStdString(), f.errorString().toStdString());
        return 0;
    }
    const QString text = QString::fromUtf8(f.readAll());
    ImportResult result;
    if (path.toLower().endsWith(".csv")) {
        result = contact_import::parseCsv(text);
    } else {
        // Default to vCard for .vcf and anything else.
        result = contact_import::parseVCard(text);
    }

    int imported = 0;
    for (const auto &c : result.contacts) {
        sip::Contact sc;
        sc.displayName = c.displayName.toStdString();
        sc.sipUri = c.sipUri.toStdString();
        sc.phone = c.phone.toStdString();
        if (m_contacts->add(sc) != sip::kInvalidContactId) imported++;
    }
    if (imported > 0 && m_model) m_model->refresh();
    spdlog::info("ContactsController::importFromFile: imported {} contacts "
                 "from {} ({} dropped)",
                 imported, path.toStdString(), result.errors);
    return imported;
}

void ContactsController::dial(int contactId)
{
    if (!m_contacts) return;
    auto c = m_contacts->findById(static_cast<sip::ContactId>(contactId));
    if (!c) return;
    emit dialRequested(QString::fromStdString(c->sipUri));
}

} // namespace compactphone

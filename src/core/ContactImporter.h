#pragma once

#include <QList>
#include <QString>

namespace compactphone {

struct ImportedContact {
    QString displayName;
    QString sipUri;  // may be empty
    QString phone;   // may be empty
};

struct ImportResult {
    QList<ImportedContact> contacts;
    int errors = 0;  // entries dropped because they had no name + no phone/uri
};

namespace contact_import {

// Parses a vCard 2.1 / 3.0 / 4.0 file. Recognized fields:
//   FN      → displayName  (preferred)
//   N       → fallback for displayName
//   TEL     → phone
//   IMPP    → sipUri        (when scheme is sip:/sips:)
//   X-SIP   → sipUri        (RFC-unofficial but common)
// Line-folding (RFC 5322 long-line continuation) is honored.
ImportResult parseVCard(const QString &text);

// Parses a CSV file. Header row required; columns are matched
// case-insensitively against:
//   name | full name | display name | first name + last name → displayName
//   phone | phone number | mobile | work phone → phone
//   sip | sip uri | sip address → sipUri
ImportResult parseCsv(const QString &text);

} // namespace contact_import
} // namespace compactphone

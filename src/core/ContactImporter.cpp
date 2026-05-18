#include "ContactImporter.h"

#include <QRegularExpression>
#include <QStringList>

namespace compactphone::contact_import {

namespace {

QStringList unfoldLines(const QString &text)
{
    // RFC 6350 §3.2: a leading SPACE or TAB continues the previous line.
    QStringList raw = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    QStringList out;
    for (const auto &line : raw) {
        if (!out.isEmpty()
            && !line.isEmpty()
            && (line[0] == ' ' || line[0] == '\t')) {
            out.last() += line.mid(1);
        } else {
            out << line;
        }
    }
    return out;
}

// Splits one vCard line "PROPERTY[;PARAM=VAL]*:VALUE" into (property, value).
// Parameters are discarded — we don't currently need TYPE etc.
bool splitProperty(const QString &line, QString &prop, QString &value)
{
    const int colon = line.indexOf(':');
    if (colon <= 0) return false;
    QString head = line.left(colon);
    value = line.mid(colon + 1);

    const int semi = head.indexOf(';');
    prop = semi >= 0 ? head.left(semi).toUpper() : head.toUpper();
    return true;
}

// Format a phone number for a SIP URI: strip everything but digits, +.
QString sanitizePhone(QString p)
{
    p.remove(QRegularExpression(QStringLiteral("[^0-9+]")));
    return p;
}

bool isSipUri(const QString &s)
{
    return s.startsWith(QStringLiteral("sip:"), Qt::CaseInsensitive)
        || s.startsWith(QStringLiteral("sips:"), Qt::CaseInsensitive);
}

} // namespace

ImportResult parseVCard(const QString &text)
{
    ImportResult result;
    const auto lines = unfoldLines(text);

    ImportedContact current;
    bool inCard = false;
    QString nFallback;

    for (const auto &raw : lines) {
        if (raw.isEmpty()) continue;
        QString prop, value;
        if (!splitProperty(raw, prop, value)) continue;

        if (prop == QStringLiteral("BEGIN")
            && value.compare(QStringLiteral("VCARD"), Qt::CaseInsensitive) == 0) {
            current = {};
            nFallback.clear();
            inCard = true;
            continue;
        }
        if (prop == QStringLiteral("END")
            && value.compare(QStringLiteral("VCARD"), Qt::CaseInsensitive) == 0) {
            if (current.displayName.isEmpty()) current.displayName = nFallback;
            if (!current.displayName.isEmpty()
                || !current.phone.isEmpty()
                || !current.sipUri.isEmpty()) {
                result.contacts << current;
            } else {
                result.errors++;
            }
            inCard = false;
            continue;
        }
        if (!inCard) continue;

        if (prop == QStringLiteral("FN")) {
            current.displayName = value.trimmed();
        } else if (prop == QStringLiteral("N")) {
            // "Last;First;Middle;Prefix;Suffix" — assemble First Last.
            const auto parts = value.split(';');
            const QString first = parts.size() > 1 ? parts[1].trimmed() : QString();
            const QString last  = parts.size() > 0 ? parts[0].trimmed() : QString();
            nFallback = (first + ' ' + last).trimmed();
        } else if (prop == QStringLiteral("TEL")) {
            if (current.phone.isEmpty()) {
                current.phone = sanitizePhone(value);
            }
        } else if (prop == QStringLiteral("IMPP")) {
            if (isSipUri(value)) current.sipUri = value.trimmed();
        } else if (prop == QStringLiteral("X-SIP")) {
            current.sipUri = value.trimmed();
        }
    }

    return result;
}

namespace {

QStringList splitCsvRow(const QString &line)
{
    QStringList out;
    QString cell;
    bool inQuotes = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line[i];
        if (inQuotes) {
            if (c == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    cell += '"';
                    ++i;
                } else {
                    inQuotes = false;
                }
            } else {
                cell += c;
            }
        } else {
            if (c == ',') {
                out << cell;
                cell.clear();
            } else if (c == '"' && cell.isEmpty()) {
                inQuotes = true;
            } else {
                cell += c;
            }
        }
    }
    out << cell;
    return out;
}

int findColumn(const QStringList &header, std::initializer_list<const char *> wanted)
{
    for (int i = 0; i < header.size(); ++i) {
        const QString h = header[i].trimmed().toLower();
        for (const char *w : wanted) {
            if (h == QString::fromUtf8(w)) return i;
        }
    }
    return -1;
}

} // namespace

ImportResult parseCsv(const QString &text)
{
    ImportResult result;
    const auto lines = text.split(QRegularExpression(QStringLiteral("\r\n|\n|\r")));
    if (lines.size() < 2) return result;

    const QStringList header = splitCsvRow(lines.first());
    const int nameCol  = findColumn(header,
        {"name", "full name", "display name", "displayname", "contact name"});
    const int firstCol = findColumn(header, {"first name", "given name", "firstname"});
    const int lastCol  = findColumn(header, {"last name", "family name", "surname", "lastname"});
    const int phoneCol = findColumn(header,
        {"phone", "phone number", "phone 1 - value", "mobile", "mobile phone",
         "work phone", "primary phone"});
    const int sipCol   = findColumn(header,
        {"sip", "sip uri", "sip address", "im - sip"});

    for (int i = 1; i < lines.size(); ++i) {
        if (lines[i].trimmed().isEmpty()) continue;
        const auto cells = splitCsvRow(lines[i]);
        ImportedContact c;

        if (nameCol >= 0 && nameCol < cells.size()) {
            c.displayName = cells[nameCol].trimmed();
        }
        if (c.displayName.isEmpty()
            && (firstCol >= 0 || lastCol >= 0)) {
            const QString f = firstCol >= 0 && firstCol < cells.size()
                ? cells[firstCol].trimmed() : QString();
            const QString l = lastCol >= 0 && lastCol < cells.size()
                ? cells[lastCol].trimmed() : QString();
            c.displayName = (f + ' ' + l).trimmed();
        }
        if (phoneCol >= 0 && phoneCol < cells.size()) {
            c.phone = sanitizePhone(cells[phoneCol]);
        }
        if (sipCol >= 0 && sipCol < cells.size()) {
            c.sipUri = cells[sipCol].trimmed();
        }

        if (!c.displayName.isEmpty()
            || !c.phone.isEmpty()
            || !c.sipUri.isEmpty()) {
            result.contacts << c;
        } else {
            result.errors++;
        }
    }

    return result;
}

} // namespace compactphone::contact_import

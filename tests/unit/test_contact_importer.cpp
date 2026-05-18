#include "core/ContactImporter.h"

#include <gtest/gtest.h>

using compactphone::contact_import::parseVCard;
using compactphone::contact_import::parseCsv;

TEST(ContactImporterVCard, ParsesFnTelImpp)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "FN:Jane Doe\n"
        "TEL;TYPE=work:+1-555-0100\n"
        "IMPP:sip:jane@example.com\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "Jane Doe");
    EXPECT_EQ(r.contacts[0].phone, "+15550100");
    EXPECT_EQ(r.contacts[0].sipUri, "sip:jane@example.com");
}

TEST(ContactImporterVCard, FallsBackToNWhenFnMissing)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "N:Smith;John;;;\n"
        "TEL:555-1234\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "John Smith");
}

TEST(ContactImporterVCard, ParsesMultipleCards)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\nVERSION:3.0\nFN:A\nTEL:1\nEND:VCARD\n"
        "BEGIN:VCARD\nVERSION:3.0\nFN:B\nTEL:2\nEND:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 2);
    EXPECT_EQ(r.contacts[0].displayName, "A");
    EXPECT_EQ(r.contacts[1].displayName, "B");
}

TEST(ContactImporterVCard, HandlesLineFolding)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "FN:Very Long Na\n"
        " me Continued\n"
        "TEL:555\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "Very Long Name Continued");
}

TEST(ContactImporterCsv, ParsesGoogleContactsLikeHeader)
{
    const QString src = QStringLiteral(
        "Name,Phone 1 - Value,SIP Address\n"
        "Alice,+1 555 0101,sip:alice@example.com\n"
        "Bob,555.0102,\n");
    const auto r = parseCsv(src);
    ASSERT_EQ(r.contacts.size(), 2);
    EXPECT_EQ(r.contacts[0].displayName, "Alice");
    EXPECT_EQ(r.contacts[0].phone, "+15550101");
    EXPECT_EQ(r.contacts[0].sipUri, "sip:alice@example.com");
    EXPECT_EQ(r.contacts[1].displayName, "Bob");
    EXPECT_EQ(r.contacts[1].phone, "5550102");
}

TEST(ContactImporterCsv, AssemblesFirstLastWhenNoFullNameColumn)
{
    const QString src = QStringLiteral(
        "First Name,Last Name,Phone\n"
        "Alice,Smith,5551111\n");
    const auto r = parseCsv(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "Alice Smith");
}

TEST(ContactImporterCsv, HandlesQuotedCellsWithCommas)
{
    const QString src = QStringLiteral(
        "Name,Phone\n"
        "\"Doe, Jane\",\"+1 (555) 0100\"\n");
    const auto r = parseCsv(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "Doe, Jane");
    EXPECT_EQ(r.contacts[0].phone, "+15550100");
}

TEST(ContactImporterVCard, IgnoresPhotoOrgNoteAndOtherFields)
{
    // vCard 4.0 contacts often include base64 photos, ORG, NOTE, EMAIL,
    // URL, etc. We don't consume those — but they must not crash the
    // parser or pollute the displayName.
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:4.0\n"
        "FN:Carol Real\n"
        "ORG:Acme Corp\n"
        "NOTE:Plays cello on Sundays\n"
        "EMAIL;TYPE=work:carol@acme.example\n"
        "URL:https://acme.example/carol\n"
        "PHOTO:data:image/jpeg;base64,/9j/4AAQSkZJRgAB\n"
        "TEL;TYPE=cell:+1 555 0177\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].displayName, "Carol Real");
    EXPECT_EQ(r.contacts[0].phone, "+15550177");
}

TEST(ContactImporterVCard, FirstTelWins)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "FN:Multi-line\n"
        "TEL;TYPE=work:+1 555 0001\n"
        "TEL;TYPE=cell:+1 555 0002\n"
        "TEL;TYPE=home:+1 555 0003\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    ASSERT_EQ(r.contacts.size(), 1);
    EXPECT_EQ(r.contacts[0].phone, "+15550001");
}

TEST(ContactImporterVCard, VCardWithNoNameNoPhoneIsDropped)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "URL:https://example.com\n"
        "END:VCARD\n");
    const auto r = parseVCard(src);
    EXPECT_TRUE(r.contacts.isEmpty());
    EXPECT_EQ(r.errors, 1);
}

TEST(ContactImporterVCard, NoEndVCardLeavesPartialEntryDropped)
{
    const QString src = QStringLiteral(
        "BEGIN:VCARD\n"
        "VERSION:3.0\n"
        "FN:Truncated\n"
        "TEL:+15551234\n");
    const auto r = parseVCard(src);
    // No END:VCARD means the entry never closes, so nothing is emitted.
    EXPECT_TRUE(r.contacts.isEmpty());
}

TEST(ContactImporterCsv, HeaderOnlyYieldsNoContacts)
{
    const QString src = QStringLiteral("Name,Phone\n");
    const auto r = parseCsv(src);
    EXPECT_TRUE(r.contacts.isEmpty());
}

TEST(ContactImporterCsv, MissingHeaderColumnsAreTolerated)
{
    const QString src = QStringLiteral(
        "Foo,Bar,Baz\n"
        "x,y,z\n");
    const auto r = parseCsv(src);
    // None of the known headers match → no displayName/phone/uri → drop.
    EXPECT_TRUE(r.contacts.isEmpty());
    EXPECT_EQ(r.errors, 1);
}

TEST(ContactImporterCsv, SkipsBlankRows)
{
    const QString src = QStringLiteral(
        "Name,Phone\n"
        "Alice,5550101\n"
        "\n"
        "Bob,5550102\n"
        "   \n"
        "Carol,5550103\n");
    const auto r = parseCsv(src);
    EXPECT_EQ(r.contacts.size(), 3);
}

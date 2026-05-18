#include <gtest/gtest.h>

#include "core/SipUri.h"

TEST(SipUri, NormalizesExtensionAgainstAccountDomain)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "600", "pbx.example.com:5060", compactphone::sip::Transport::Udp),
              "sip:600@pbx.example.com:5060");
}

TEST(SipUri, UsesSipsSchemeForTlsAccounts)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "600", "pbx.example.com:5061", compactphone::sip::Transport::Tls),
              "sips:600@pbx.example.com:5061");
}

TEST(SipUri, PreservesExplicitSipUri)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "sips:alice@example.com", "pbx.example.com:5061",
                  compactphone::sip::Transport::Udp),
              "sips:alice@example.com");
}

TEST(SipUri, AddsSchemeToBareAddress)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "alice@example.com", "pbx.example.com:5060",
                  compactphone::sip::Transport::Udp),
              "sip:alice@example.com");
}

TEST(SipUri, ReturnsEmptyForEmptyOrWhitespace)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "", "pbx.example.com", compactphone::sip::Transport::Udp),
              "");
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "   \t\n  ", "pbx.example.com", compactphone::sip::Transport::Udp),
              "");
}

TEST(SipUri, TrimsLeadingAndTrailingWhitespace)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "  600  ", "pbx.example.com:5060",
                  compactphone::sip::Transport::Udp),
              "sip:600@pbx.example.com:5060");
}

TEST(SipUri, RecognizesMixedCaseScheme)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "SIP:alice@example.com", "pbx.example.com",
                  compactphone::sip::Transport::Udp),
              "SIP:alice@example.com");
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "Sips:bob@example.com", "pbx.example.com",
                  compactphone::sip::Transport::Tls),
              "Sips:bob@example.com");
}

TEST(SipUri, UsesSipSchemeForTcpAndUdpAlike)
{
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "600", "pbx.example.com:5060", compactphone::sip::Transport::Tcp),
              "sip:600@pbx.example.com:5060");
}

TEST(SipUri, OmitsAtHostWhenDomainEmptyAndTargetIsBareUser)
{
    // No account domain configured yet, but user typed just an extension —
    // we return the scheme + target without an "@" tail (no host to append).
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "600", "", compactphone::sip::Transport::Udp),
              "sip:600");
}

TEST(SipUri, KeepsExplicitSipEvenWhenAccountIsTls)
{
    // Caller explicitly chose sip:; we don't silently upgrade to sips:.
    EXPECT_EQ(compactphone::sip::normalizeSipTarget(
                  "sip:alice@example.com", "pbx.example.com:5061",
                  compactphone::sip::Transport::Tls),
              "sip:alice@example.com");
}

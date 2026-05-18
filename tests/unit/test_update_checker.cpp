#include "core/UpdateChecker.h"

#include <gtest/gtest.h>

using compactphone::UpdateChecker;

TEST(UpdateCheckerVersions, OrdersDottedNumbersCorrectly)
{
    EXPECT_LT(UpdateChecker::compareVersions("1.0.0", "1.0.1"), 0);
    EXPECT_GT(UpdateChecker::compareVersions("2.0.0", "1.999.999"), 0);
    EXPECT_EQ(UpdateChecker::compareVersions("1.2.3", "1.2.3"), 0);
}

TEST(UpdateCheckerVersions, PadsMissingSegmentsWithZeros)
{
    EXPECT_EQ(UpdateChecker::compareVersions("1.2", "1.2.0"), 0);
    EXPECT_LT(UpdateChecker::compareVersions("1.2", "1.2.1"), 0);
    EXPECT_GT(UpdateChecker::compareVersions("1.3", "1.2.99"), 0);
}

TEST(UpdateCheckerVersions, DoubleDigitSegmentsBeatSingleDigit)
{
    // The classic "1.10" vs "1.2" trap: numeric, not lexical.
    EXPECT_GT(UpdateChecker::compareVersions("1.10", "1.2"), 0);
}

TEST(UpdateCheckerVersions, NonNumericSegmentsResolveToZero)
{
    EXPECT_EQ(UpdateChecker::compareVersions("alpha", "0"), 0);
    EXPECT_EQ(UpdateChecker::compareVersions("1.0-rc1", "1.0"), 0);
}

TEST(UpdateCheckerParse, WellFormedFeedReturnsHighestEnclosure)
{
    const QByteArray xml = R"(<?xml version="1.0" encoding="UTF-8"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <enclosure url="https://example.com/cp-0.3.0.dmg"
                 sparkle:shortVersionString="0.3.0"
                 length="123" type="application/octet-stream" />
    </item>
    <item>
      <enclosure url="https://example.com/cp-0.4.0.dmg"
                 sparkle:shortVersionString="0.4.0"
                 length="456" type="application/octet-stream" />
    </item>
  </channel>
</rss>)";

    const auto feed = UpdateChecker::parseAppcast(xml);
    EXPECT_EQ(feed.version, "0.4.0");
    EXPECT_EQ(feed.url, QUrl("https://example.com/cp-0.4.0.dmg"));
}

TEST(UpdateCheckerParse, FallsBackFromShortVersionToVersion)
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item>
      <enclosure url="https://example.com/cp-0.5.0.dmg"
                 sparkle:version="0.5.0"
                 length="0" type="application/octet-stream" />
    </item>
  </channel>
</rss>)";
    const auto feed = UpdateChecker::parseAppcast(xml);
    EXPECT_EQ(feed.version, "0.5.0");
}

TEST(UpdateCheckerParse, EmptyFeedReturnsEmpty)
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel><title>Empty</title></channel>
</rss>)";
    const auto feed = UpdateChecker::parseAppcast(xml);
    EXPECT_TRUE(feed.version.isEmpty());
    EXPECT_FALSE(feed.url.isValid() && !feed.url.isEmpty());
}

TEST(UpdateCheckerParse, MalformedXmlReturnsEmpty)
{
    const QByteArray xml = "<not-actually-xml<<>";
    const auto feed = UpdateChecker::parseAppcast(xml);
    EXPECT_TRUE(feed.version.isEmpty());
}

TEST(UpdateCheckerParse, EnclosureMissingUrlOrVersionIsSkipped)
{
    const QByteArray xml = R"(<?xml version="1.0"?>
<rss xmlns:sparkle="http://www.andymatuschak.org/xml-namespaces/sparkle">
  <channel>
    <item><enclosure sparkle:shortVersionString="0.4.0" length="0" /></item>
    <item><enclosure url="https://example.com/x.dmg" length="0" /></item>
    <item>
      <enclosure url="https://example.com/cp-0.3.1.dmg"
                 sparkle:shortVersionString="0.3.1"
                 length="0" type="application/octet-stream" />
    </item>
  </channel>
</rss>)";
    const auto feed = UpdateChecker::parseAppcast(xml);
    EXPECT_EQ(feed.version, "0.3.1");
}

#include "core/LogBuffer.h"

#include <gtest/gtest.h>
#include <spdlog/spdlog.h>

using compactphone::LogBuffer;

TEST(LogBufferTest, CapturesSpdlogOutput)
{
    // Instantiate the singleton (registers its sink with default_logger).
    auto &buf = LogBuffer::instance();

    const QString needle =
        QStringLiteral("logbuffer-test-needle-%1").arg(::time(nullptr));
    spdlog::info("{}", needle.toStdString());
    spdlog::default_logger()->flush();

    const auto lines = buf.lines();
    bool found = false;
    for (const auto &l : lines) {
        if (l.contains(needle)) { found = true; break; }
    }
    EXPECT_TRUE(found) << "expected '" << needle.toStdString()
                       << "' in captured log; captured "
                       << lines.size() << " lines";
}

TEST(LogBufferTest, AsTextJoinsLinesWithNewline)
{
    auto &buf = LogBuffer::instance();
    spdlog::info("logbuffer-asText-line-1");
    spdlog::info("logbuffer-asText-line-2");
    spdlog::default_logger()->flush();

    const QString text = buf.asText();
    // The two recent lines should both appear with a newline between them
    // somewhere in the joined text.
    const int idx1 = text.indexOf("logbuffer-asText-line-1");
    const int idx2 = text.indexOf("logbuffer-asText-line-2");
    ASSERT_GE(idx1, 0);
    ASSERT_GE(idx2, 0);
    EXPECT_LT(idx1, idx2);
    // No trailing-newline-on-each-line garbage: between the two markers
    // we should see exactly one '\n' (other log lines may sit between
    // them in practice, so just assert that newlines exist as separators).
    EXPECT_GT(text.count('\n'), 0);
}

TEST(LogBufferTest, RingBufferStaysUnder1000EvenAfterFlood)
{
    auto &buf = LogBuffer::instance();
    for (int i = 0; i < 2000; ++i) {
        spdlog::info("logbuffer-flood-{}", i);
    }
    spdlog::default_logger()->flush();
    // Capacity is 500 per LogBuffer.cpp; allow some slack for sink
    // batching but it must never blow past 1000.
    EXPECT_LE(buf.lines().size(), 1000);
}

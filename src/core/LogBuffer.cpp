#include "LogBuffer.h"

#include <spdlog/spdlog.h>
#include <spdlog/sinks/ringbuffer_sink.h>

#include <memory>

namespace compactphone {

struct LogBuffer::Impl {
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink;
};

namespace {
constexpr size_t kCapacity = 500;
}

LogBuffer::LogBuffer() : m_impl(new Impl)
{
    m_impl->sink =
        std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(kCapacity);
    m_impl->sink->set_pattern("%Y-%m-%d %H:%M:%S.%e [%l] %v");

    if (auto def = spdlog::default_logger()) {
        def->sinks().push_back(m_impl->sink);
    }
}

LogBuffer &LogBuffer::instance()
{
    static LogBuffer buf;
    return buf;
}

QStringList LogBuffer::lines() const
{
    const auto raw = m_impl->sink->last_formatted();
    QStringList out;
    out.reserve(static_cast<int>(raw.size()));
    for (const auto &s : raw) {
        QString line = QString::fromStdString(s);
        // ringbuffer_sink leaves the formatter's trailing newline; trim.
        while (!line.isEmpty() && (line.back() == '\n' || line.back() == '\r')) {
            line.chop(1);
        }
        out << line;
    }
    return out;
}

QString LogBuffer::asText() const
{
    return lines().join('\n');
}

} // namespace compactphone

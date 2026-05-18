#pragma once

#include <QString>
#include <QStringList>

namespace compactphone {

// Tail of the structured spdlog stream, kept in memory for the in-app
// log viewer and the "Export diagnostics" file. Installs a ringbuffer
// sink onto the default spdlog logger the first time it is constructed.
class LogBuffer {
public:
    // Returns the singleton; constructs it (and registers the sink) on
    // first access.
    static LogBuffer &instance();

    // Most-recent-first list of formatted log lines, up to capacity.
    QStringList lines() const;

    // Joined view: each entry on its own line, oldest first. Useful as
    // the body of a diagnostics file.
    QString asText() const;

private:
    LogBuffer();
    ~LogBuffer() = default;

    LogBuffer(const LogBuffer &) = delete;
    LogBuffer &operator=(const LogBuffer &) = delete;

    struct Impl;
    Impl *m_impl;
};

} // namespace compactphone

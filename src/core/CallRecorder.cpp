#include "CallRecorder.h"

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

namespace compactphone::sip {

CallRecorder::CallRecorder() = default;
CallRecorder::~CallRecorder() = default;

bool CallRecorder::start(std::int32_t id, pj::AudioMedia *aud,
                         const std::string &outputPath)
{
    if (!aud || outputPath.empty()) return false;
    if (m_recorders.count(id)) return true; // already recording
    try {
        auto rec = std::make_unique<pj::AudioMediaRecorder>();
        rec->createRecorder(outputPath);
        // Mix both directions into the recorder.
        auto &mgr = pj::Endpoint::instance().audDevManager();
        mgr.getCaptureDevMedia().startTransmit(*rec);
        aud->startTransmit(*rec);
        m_recorders[id] = std::move(rec);
        spdlog::info("CallRecorder: recording call {} -> {}", id, outputPath);
        return true;
    } catch (const pj::Error &e) {
        spdlog::error("CallRecorder::start: {}", e.info());
        return false;
    }
}

bool CallRecorder::stop(std::int32_t id)
{
    auto it = m_recorders.find(id);
    if (it == m_recorders.end()) return false;
    // Destroying the recorder flushes and closes the WAV file.
    m_recorders.erase(it);
    spdlog::info("CallRecorder: stopped recording call {}", id);
    return true;
}

bool CallRecorder::isRecording(std::int32_t id) const
{
    return m_recorders.count(id) > 0;
}

void CallRecorder::drop(std::int32_t id)
{
    m_recorders.erase(id);
}

} // namespace compactphone::sip

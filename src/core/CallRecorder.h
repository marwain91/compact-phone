#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>

namespace pj {
class AudioMedia;
class AudioMediaRecorder;
}

namespace compactphone::sip {

// Owns the per-call WAV recorders. Extracted from CallManager so the recording
// lifecycle is testable and self-contained; CallManager finds the call's
// active audio media and hands it here. Keys match CallManager::CallId
// (std::int32_t).
class CallRecorder {
public:
    CallRecorder();
    ~CallRecorder();

    CallRecorder(const CallRecorder &) = delete;
    CallRecorder &operator=(const CallRecorder &) = delete;

    // Begin mixing both directions (capture device + the call's audio) into a
    // WAV at outputPath. No-op success if already recording this call. Returns
    // false if aud is null or PJSIP rejects the recorder.
    bool start(std::int32_t id, pj::AudioMedia *aud, const std::string &outputPath);

    // Flush + close the WAV. Returns false if this call wasn't recording.
    bool stop(std::int32_t id);

    bool isRecording(std::int32_t id) const;

    // Drop the recorder for an ended call (closes the file if still open),
    // without the "stopped recording" log line.
    void drop(std::int32_t id);

private:
    std::unordered_map<std::int32_t, std::unique_ptr<pj::AudioMediaRecorder>>
        m_recorders;
};

} // namespace compactphone::sip

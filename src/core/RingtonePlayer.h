#pragma once

#include <memory>
#include <string>

namespace pj { class AudioMediaPlayer; }

namespace compactphone::sip {

// Loops a WAV file through PJSIP's playback device. Lazy-constructs the
// underlying pj::AudioMediaPlayer on first start(). Both start() and stop()
// are idempotent.
class RingtonePlayer {
public:
    explicit RingtonePlayer(std::string wavPath);
    ~RingtonePlayer();

    RingtonePlayer(const RingtonePlayer &) = delete;
    RingtonePlayer &operator=(const RingtonePlayer &) = delete;

    void start();
    void stop();
    void setPath(const std::string &p);

    bool isPlaying() const { return m_playing; }
    const std::string &path() const { return m_path; }

private:
    std::string m_path;
    std::unique_ptr<pj::AudioMediaPlayer> m_player;
    bool m_playing = false;
};

} // namespace compactphone::sip

#include "RingtonePlayer.h"

#include <pjsua2.hpp>
#include <spdlog/spdlog.h>

#include <utility>

namespace compactphone::sip {

RingtonePlayer::RingtonePlayer(std::string wavPath) : m_path(std::move(wavPath)) {}

void RingtonePlayer::setPath(const std::string &p)
{
    if (m_path == p) return;
    const bool wasPlaying = m_playing;
    if (wasPlaying) try { stop(); } catch (...) {}
    m_player.reset();   // force re-create on next start with new path
    m_path = p;
    if (wasPlaying) try { start(); } catch (...) {}
}

RingtonePlayer::~RingtonePlayer()
{
    if (m_playing) {
        try { stop(); } catch (...) {}
    }
}

void RingtonePlayer::start()
{
    if (m_playing) return;
    if (m_path.empty()) {
        spdlog::warn("RingtonePlayer::start: no ringtone path configured");
        return;
    }
    try {
        if (!m_player) {
            m_player = std::make_unique<pj::AudioMediaPlayer>();
            m_player->createPlayer(m_path, 0);   // 0 = loop
        }
        auto &mgr = pj::Endpoint::instance().audDevManager();
        m_player->startTransmit(mgr.getPlaybackDevMedia());
        m_playing = true;
        spdlog::info("RingtonePlayer started: {}", m_path);
    } catch (const pj::Error &e) {
        spdlog::error("RingtonePlayer::start: {}", e.info());
    }
}

void RingtonePlayer::stop()
{
    if (!m_playing) return;
    try {
        auto &mgr = pj::Endpoint::instance().audDevManager();
        m_player->stopTransmit(mgr.getPlaybackDevMedia());
        m_playing = false;
        spdlog::info("RingtonePlayer stopped");
    } catch (const pj::Error &e) {
        spdlog::error("RingtonePlayer::stop: {}", e.info());
    }
}

} // namespace compactphone::sip

#pragma once
#include "physics.hpp"
#include "sound_mixer.hpp"
#include <SDL3/SDL.h>
#include <cstddef>
#include <cstdint>
#include <mutex>
namespace toy {
struct AudioVoiceStats {
    size_t active = 0;
    size_t peak = 0;
    uint64_t replaced = 0;
};

class Audio {
  public:
    Audio();
    ~Audio();
    void play(const std::vector<Impact> &impacts);
    void toggleMute();
    void setVolume(float volume);
    bool muted() const { return muted_; }
    float volume() const { return volume_; }
    AudioVoiceStats voiceStats() const;

  private:
    static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total);
    SDL_AudioStream *stream_ = nullptr;
    mutable std::mutex mutex_;
    SoundMixer mixer_;
    bool muted_ = false;
    float volume_ = 1.0f;
};
} // namespace toy

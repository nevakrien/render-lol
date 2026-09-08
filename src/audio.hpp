#pragma once
#include "physics.hpp"
#include "sound_mixer.hpp"
#include <SDL3/SDL.h>
#include <mutex>
namespace toy {
class Audio {
  public:
    Audio();
    ~Audio();
    void play(const std::vector<Impact> &impacts);
    void toggleMute();
    void setVolume(float volume);
    bool muted() const { return muted_; }
    float volume() const { return volume_; }

  private:
    static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total);
    SDL_AudioStream *stream_ = nullptr;
    std::mutex mutex_;
    SoundMixer mixer_;
    bool muted_ = false;
    float volume_ = 1.0f;
};
} // namespace toy

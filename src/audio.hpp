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
    bool muted() const { return muted_; }

  private:
    static void SDLCALL feed(void *userdata, SDL_AudioStream *stream, int additional, int total);
    SDL_AudioStream *stream_ = nullptr;
    std::mutex mutex_;
    SoundMixer mixer_;
    bool muted_ = false;
};
} // namespace toy

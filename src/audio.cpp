#include "audio.hpp"
#include <algorithm>
#include <array>

namespace toy {
Audio::Audio() {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, SoundMixer::sampleRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
    if (stream_)
        SDL_ResumeAudioStreamDevice(stream_);
    else
        SDL_Log("Audio unavailable: %s", SDL_GetError());
}
Audio::~Audio() {
    // SDL waits for the callback before destroying the stream; mixer state
    // and its mutex remain alive until that has completed.
    if (stream_)
        SDL_DestroyAudioStream(stream_);
}
void Audio::toggleMute() {
    std::lock_guard<std::mutex> lock(mutex_);
    muted_ = !muted_;
    mixer_.clear();
}
void Audio::play(const std::vector<Impact> &impacts) {
    if (!stream_ || impacts.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (muted_)
        return;
    for (auto &hit : impacts)
        mixer_.trigger(220 + float(hit.b % 7) * 55, std::min(.15f, hit.strength * .02f));
}
void SDLCALL Audio::feed(void *userdata, SDL_AudioStream *stream, int additional, int) {
    auto &self = *static_cast<Audio *>(userdata);
    std::array<float, 512> samples;
    int remaining = (additional + int(sizeof(float)) - 1) / int(sizeof(float));
    while (remaining > 0) {
        int count = std::min(remaining, int(samples.size()));
        {
            std::lock_guard<std::mutex> lock(self.mutex_);
            self.mixer_.render(samples.data(), size_t(count));
        }
        if (!SDL_PutAudioStreamData(stream, samples.data(), count * int(sizeof(float))))
            return;
        remaining -= count;
    }
}
} // namespace toy

#include "audio.hpp"
#include "gameplay_tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>

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
void Audio::setVolume(float volume) {
    std::lock_guard<std::mutex> lock(mutex_);
    volume_ = std::clamp(volume, 0.0f, 2.0f);
}
void Audio::play(const std::vector<Impact> &impacts) {
    if (!stream_ || impacts.empty())
        return;
    std::lock_guard<std::mutex> lock(mutex_);
    if (muted_)
        return;
    for (auto &hit : impacts) {
        if (hit.strength < tuning::minimumAudibleImpact || hit.heat >= tuning::heatBurnout)
            continue;
        float critical = std::clamp((hit.heat - tuning::audioFadeHeat) /
                                        (tuning::heatBurnout - tuning::audioFadeHeat),
                                    0.0f, 1.0f);
        float liveness = tuning::normalizedHeatPower(hit.heat);
        float detune = 1.0f +
                       (hit.seed - .5f) * (tuning::baseDetune + tuning::heatDetune * liveness);
        float heatPosition = std::clamp(
            hit.heat * tuning::heatPitchSensitivity / tuning::heatBurnout, 0.0f, 1.0f);
        float curvedHeat = heatPosition * heatPosition * (3.0f - 2.0f * heatPosition);
        float massScale = std::pow(tuning::referenceImpactMass / hit.effectiveMass,
                                   tuning::massPitchExponent);
        float coldPitch = tuning::coldImpactFrequency * massScale;
        float hotPitch = tuning::hotImpactFrequency * massScale;
        float pitch = coldPitch * std::pow(hotPitch / coldPitch, curvedHeat) * detune;
        float amplitude =
            (tuning::baseImpactAmplitude +
             std::min(hit.strength, tuning::maximumAudioStrength) *
                  tuning::impactAmplitudeScale) *
            liveness * (1.0f - critical);
        // IEC 61672 A-weighting, used here as a relative digital loudness correction.
        float frequencySquared = pitch * pitch;
        constexpr float lowCornerSquared = 20.6f * 20.6f;
        constexpr float middleLowSquared = 107.7f * 107.7f;
        constexpr float middleHighSquared = 737.9f * 737.9f;
        constexpr float highCornerSquared = 12200.0f * 12200.0f;
        float response = highCornerSquared * frequencySquared * frequencySquared /
                         ((frequencySquared + lowCornerSquared) *
                          std::sqrt((frequencySquared + middleLowSquared) *
                                    (frequencySquared + middleHighSquared)) *
                          (frequencySquared + highCornerSquared));
        float aWeightingDecibels = 20.0f * std::log10(response) + 2.0f;
        float weightingAmount = std::clamp(tuning::impactAWeightingAmount, 0.0f, 1.0f);
        float maximumRawDecibels = tuning::maximumAWeightedImpactDecibels -
                                   weightingAmount * aWeightingDecibels;
        float maximumAmplitude = std::pow(10.0f, maximumRawDecibels / 20.0f);
        amplitude = std::min(amplitude, maximumAmplitude);
        mixer_.trigger(pitch, amplitude);
    }
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
            for (int i = 0; i < count; ++i)
                samples[i] = std::clamp(samples[i] * self.volume_, -1.0f, 1.0f);
        }
        if (!SDL_PutAudioStreamData(stream, samples.data(), count * int(sizeof(float))))
            return;
        remaining -= count;
    }
}
} // namespace toy

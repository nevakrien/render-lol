#include "audio.hpp"
#include "gameplay_tuning.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <random>

namespace toy {
namespace {
constexpr float tau = 6.2831853f;

float noiseSample(uint32_t seed, int age) {
    uint32_t noise = seed + uint32_t(age) * 747796405u;
    noise ^= noise >> 16;
    noise *= 2246822519u;
    noise ^= noise >> 13;
    return float(noise & 0xffffu) / 32767.5f - 1.0f;
}

uint32_t entropySeed() {
    std::random_device entropy;
    uint32_t seed = entropy();
    seed ^= entropy() + 0x9e3779b9u + (seed << 6) + (seed >> 2);
    return seed ? seed : 0x6d2b79f5u;
}
} // namespace

Audio::Audio() : mixer_(tuning::maximumSimultaneousImpactVoices), randomState_(entropySeed()) {
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_Log("Audio unavailable: %s", SDL_GetError());
        return;
    }
    SDL_AudioSpec spec{SDL_AUDIO_F32, 1, sampleRate};
    stream_ = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, feed, this);
    if (stream_) {
        SDL_AudioSpec source{}, device{};
        if (SDL_GetAudioStreamFormat(stream_, &source, &device)) {
            SDL_Log("Audio stream: source %d Hz/%d channel(s)/format 0x%x, device %d "
                    "Hz/%d channel(s)/format 0x%x",
                    source.freq, source.channels, unsigned(source.format), device.freq,
                    device.channels, unsigned(device.format));
        } else {
            SDL_Log("Could not query audio stream format: %s", SDL_GetError());
        }
        SDL_ResumeAudioStreamDevice(stream_);
    } else
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
AudioVoiceStats Audio::voiceStats() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {mixer_.activeVoices(), mixer_.peakVoices(), mixer_.replacedVoices()};
}
float Audio::randomUnit() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return float(randomState_ >> 8) / 16777216.0f;
}
std::vector<float> Audio::makeImpactSound(float frequency, float amplitude, float speed,
                                          float maximumAmplitude) {
    const int duration = int(sampleRate * tuning::impactDurationSeconds);
    std::vector<float> samples(static_cast<size_t>(duration));
    float texture = std::pow(std::clamp(speed, 0.0f, 1.0f), tuning::impactTextureExponent);
    float pitchVariation = tuning::basePitchVariation + tuning::texturePitchVariation * texture;
    frequency *= 1.0f + (randomUnit() - .5f) * pitchVariation;
    float phase = randomUnit() * tau * texture;
    float secondModeRatio = tuning::secondModeFrequencyRatio +
                            (randomUnit() - .5f) * tuning::secondModeRatioVariation * texture;
    float thirdModeRatio = tuning::thirdModeFrequencyRatio +
                           (randomUnit() - .5f) * tuning::thirdModeRatioVariation * texture;
    uint32_t noiseSeed = randomState_;
    randomUnit();
    float attackSeconds = tuning::slowImpactAttackSeconds +
                          (tuning::fastImpactAttackSeconds - tuning::slowImpactAttackSeconds) *
                              texture;
    float decayRate = tuning::baseToneDecayRate + tuning::speedToneDecayRate * texture;
    float secondAmount = tuning::fastSecondModeAmount * texture;
    float thirdAmount = tuning::fastThirdModeAmount * texture;
    float fundamentalAmount = 1.0f - secondAmount - thirdAmount;
    float noiseAmount = tuning::fastNoiseAmount * texture;
    float releaseSamples = tuning::impactReleaseSeconds * sampleRate;
    for (int age = 0; age < duration; ++age) {
        float t = float(age) / sampleRate;
        float attack = std::min(1.0f, t / attackSeconds);
        float release = std::min(1.0f, float(duration - age) / releaseSamples);
        float angle = tau * frequency * t + phase;
        float tone = fundamentalAmount * std::sin(angle) +
                     secondAmount * std::sin(secondModeRatio * angle) +
                     thirdAmount * std::sin(thirdModeRatio * angle);
        float noiseAttack = std::min(1.0f, t / tuning::noiseAttackSeconds);
        float transient = noiseAmount * noiseAttack * std::exp(-tuning::noiseDecayRate * t) *
                          noiseSample(noiseSeed, age);
        samples[size_t(age)] =
            amplitude * release * (attack * std::exp(-decayRate * t) * tone + transient);
    }
    float peak = 0.0f;
    for (float sample : samples)
        peak = std::max(peak, std::abs(sample));
    if (peak > maximumAmplitude) {
        float scale = maximumAmplitude / peak;
        for (float &sample : samples)
            sample *= scale;
    }
    return samples;
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
        float pitchPosition = std::clamp(
            tuning::heatPitchWeight * tuning::normalizedHeatPower(hit.heat) +
                tuning::impactSpeedPitchWeight * hit.physicalStrength,
            0.0f, 1.0f);
        float curvedPitch = pitchPosition * pitchPosition * (3.0f - 2.0f * pitchPosition);
        float massScale = std::pow(tuning::referenceImpactMass / hit.effectiveMass,
                                   tuning::massPitchExponent);
        float minimumPitch = tuning::minimumImpactFrequency * massScale;
        float maximumPitch = tuning::maximumImpactFrequency * massScale;
        float pitch = minimumPitch * std::pow(maximumPitch / minimumPitch, curvedPitch);
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
        mixer_.play(makeImpactSound(pitch, amplitude, hit.physicalStrength, maximumAmplitude));
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
            for (int i = 0; i < count; ++i) {
                float magnitude = std::abs(samples[i]);
                if (magnitude > tuning::limiterThreshold) {
                    float headroom = 1.0f - tuning::limiterThreshold;
                    magnitude = tuning::limiterThreshold +
                                headroom * std::tanh((magnitude - tuning::limiterThreshold) /
                                                     headroom);
                    samples[i] = std::copysign(magnitude, samples[i]);
                }
                samples[i] = std::clamp(samples[i] * self.volume_, -1.0f, 1.0f);
            }
        }
        if (!SDL_PutAudioStreamData(stream, samples.data(), count * int(sizeof(float))))
            return;
        remaining -= count;
    }
}
} // namespace toy

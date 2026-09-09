#include "sound_mixer.hpp"
#include <algorithm>
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

SoundMixer::SoundMixer() : SoundMixer(entropySeed()) {}

SoundMixer::SoundMixer(uint32_t randomSeed) : randomState_(randomSeed ? randomSeed : 0x6d2b79f5u) {}

float SoundMixer::randomUnit() {
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return float(randomState_ >> 8) / 16777216.0f;
}

void SoundMixer::trigger(float frequency, float amplitude, float speed) {
    auto slot = std::find_if(voices_.begin(), voices_.end(),
                             [](const Voice &v) { return v.age >= duration; });
    if (slot == voices_.end()) {
        // Replace the quietest decaying voice when all slots are occupied.
        // A new impact starts now, never after an older sound finishes.
        auto level = [](const Voice &v) {
            float decayRate = tuning::baseToneDecayRate + tuning::speedToneDecayRate * v.texture;
            return v.amplitude * std::exp(-decayRate * v.age / sampleRate);
        };
        slot =
            std::min_element(voices_.begin(), voices_.end(),
                             [&](const Voice &a, const Voice &b) { return level(a) < level(b); });
        ++replacedVoices_;
    } else {
        ++activeVoices_;
        peakVoices_ = std::max(peakVoices_, activeVoices_);
    }
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
    *slot = {frequency, amplitude, texture, phase, secondModeRatio, thirdModeRatio, noiseSeed, 0};
}

void SoundMixer::render(float *output, size_t samples) {
    std::fill(output, output + samples, 0.f);
    for (auto &voice : voices_) {
        if (voice.age >= duration)
            continue;
        float attackSeconds =
            tuning::slowImpactAttackSeconds +
            (tuning::fastImpactAttackSeconds - tuning::slowImpactAttackSeconds) * voice.texture;
        float decayRate = tuning::baseToneDecayRate + tuning::speedToneDecayRate * voice.texture;
        float secondAmount = tuning::fastSecondModeAmount * voice.texture;
        float thirdAmount = tuning::fastThirdModeAmount * voice.texture;
        float fundamentalAmount = 1.0f - secondAmount - thirdAmount;
        float noiseAmount = tuning::fastNoiseAmount * voice.texture;
        float releaseSamples = tuning::impactReleaseSeconds * sampleRate;

        for (size_t i = 0; i < samples && voice.age < duration; ++i, ++voice.age) {
            float t = float(voice.age) / sampleRate;
            float attack = std::min(1.0f, t / attackSeconds);
            float release = std::min(1.0f, float(duration - voice.age) / releaseSamples);
            float angle = tau * voice.frequency * t + voice.phase;
            float tone = fundamentalAmount * std::sin(angle) +
                         secondAmount * std::sin(voice.secondModeRatio * angle) +
                         thirdAmount * std::sin(voice.thirdModeRatio * angle);
            float noiseAttack = std::min(1.0f, t / tuning::noiseAttackSeconds);
            float transient = noiseAmount * noiseAttack * std::exp(-tuning::noiseDecayRate * t) *
                              noiseSample(voice.noiseSeed, voice.age);
            output[i] +=
                voice.amplitude * release * (attack * std::exp(-decayRate * t) * tone + transient);
        }
        if (voice.age >= duration)
            --activeVoices_;
    }
    for (size_t i = 0; i < samples; ++i) {
        float magnitude = std::abs(output[i]);
        if (magnitude > tuning::limiterThreshold) {
            float headroom = 1.0f - tuning::limiterThreshold;
            magnitude = tuning::limiterThreshold +
                        headroom * std::tanh((magnitude - tuning::limiterThreshold) / headroom);
            output[i] = std::copysign(magnitude, output[i]);
        }
    }
}

void SoundMixer::clear() {
    for (auto &voice : voices_)
        voice.age = duration;
    activeVoices_ = 0;
}
} // namespace toy

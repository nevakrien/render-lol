#pragma once
#include "gameplay_tuning.hpp"
#include <array>
#include <cstddef>
#include <cstdint>

namespace toy {
// Sample-based state, independent of SDL and the simulation tick rate.
// The caller synchronizes access when used across threads.
class SoundMixer {
  public:
    static constexpr int sampleRate = 48000;
    static constexpr int duration = int(sampleRate * tuning::impactDurationSeconds);
    SoundMixer();
    explicit SoundMixer(uint32_t randomSeed);
    void trigger(float frequency, float amplitude, float speed = 0.0f);
    void render(float *output, size_t samples);
    void clear();
    size_t activeVoices() const { return activeVoices_; }
    size_t peakVoices() const { return peakVoices_; }
    uint64_t replacedVoices() const { return replacedVoices_; }

  private:
    struct Voice {
        float frequency = 0, amplitude = 0, texture = 0, phase = 0;
        float secondModeRatio = 0, thirdModeRatio = 0;
        uint32_t noiseSeed = 0;
        int age = duration;
    };
    float randomUnit();

    std::array<Voice, tuning::maximumSimultaneousImpactVoices> voices_{};
    uint32_t randomState_;
    size_t activeVoices_ = 0;
    size_t peakVoices_ = 0;
    uint64_t replacedVoices_ = 0;
};
} // namespace toy

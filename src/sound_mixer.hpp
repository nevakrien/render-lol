#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>

namespace toy {
// Retains and sums sample buffers. The caller synchronizes cross-thread access.
class SoundMixer {
  public:
    explicit SoundMixer(size_t maximumVoices);
    void play(std::vector<float> samples);
    void render(float *output, size_t samples);
    void clear();
    size_t activeVoices() const { return activeVoices_; }
    size_t peakVoices() const { return peakVoices_; }
    uint64_t replacedVoices() const { return replacedVoices_; }

  private:
    struct Voice {
        std::vector<float> samples;
        size_t position = 0;
    };

    std::vector<Voice> voices_;
    size_t maximumVoices_;
    size_t activeVoices_ = 0;
    size_t peakVoices_ = 0;
    uint64_t replacedVoices_ = 0;
};
} // namespace toy

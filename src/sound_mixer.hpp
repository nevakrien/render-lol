#pragma once
#include <array>
#include <cstddef>

namespace toy {
// Sample-based state, independent of SDL and the simulation tick rate.
// The caller synchronizes access when used across threads.
class SoundMixer {
  public:
    static constexpr int sampleRate = 48000;
    static constexpr int duration = 4800;
    void trigger(float frequency, float amplitude);
    void render(float *output, size_t samples);
    void clear();

  private:
    struct Voice {
        float frequency = 0, amplitude = 0;
        int age = duration;
    };
    std::array<Voice, 64> voices_{};
};
} // namespace toy

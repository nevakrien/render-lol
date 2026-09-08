#include "sound_mixer.hpp"
#include <algorithm>
#include <cmath>

namespace toy {
void SoundMixer::trigger(float frequency, float amplitude) {
    auto slot = std::find_if(voices_.begin(), voices_.end(),
                             [](const Voice &v) { return v.age >= duration; });
    if (slot == voices_.end()) {
        // Replace the quietest decaying voice when all slots are occupied.
        // A new impact starts now, never after an older sound finishes.
        auto level = [](const Voice &v) {
            return v.amplitude * std::exp(-55.f * v.age / sampleRate);
        };
        slot =
            std::min_element(voices_.begin(), voices_.end(),
                             [&](const Voice &a, const Voice &b) { return level(a) < level(b); });
    }
    *slot = {frequency, amplitude, 0};
}
void SoundMixer::render(float *output, size_t samples) {
    std::fill(output, output + samples, 0.f);
    for (auto &voice : voices_) {
        for (size_t i = 0; i < samples && voice.age < duration; ++i, ++voice.age) {
            float t = float(voice.age) / sampleRate;
            float attack = std::min(1.f, t / .003f);
            float release = std::min(1.f, float(duration - voice.age) / 240);
            output[i] += voice.amplitude * attack * release * std::exp(-55 * t) *
                         std::sin(6.2831853f * voice.frequency * t);
        }
    }
    for (size_t i = 0; i < samples; ++i)
        output[i] = std::clamp(output[i], -1.f, 1.f);
}
void SoundMixer::clear() {
    for (auto &voice : voices_)
        voice.age = duration;
}
} // namespace toy

#include "sound_mixer.hpp"
#include <algorithm>
#include <cmath>

namespace toy {
SoundMixer::SoundMixer(size_t maximumVoices) : maximumVoices_(maximumVoices) {
    voices_.reserve(maximumVoices);
}

void SoundMixer::play(std::vector<float> samples) {
    if (samples.empty() || maximumVoices_ == 0)
        return;
    if (voices_.size() == maximumVoices_) {
        auto remainingPeak = [](const Voice &voice) {
            float peak = 0.0f;
            for (size_t i = voice.position; i < voice.samples.size(); ++i)
                peak = std::max(peak, std::abs(voice.samples[i]));
            return peak;
        };
        auto slot = std::min_element(voices_.begin(), voices_.end(), [&](const Voice &a, const Voice &b) {
            return remainingPeak(a) < remainingPeak(b);
        });
        *slot = {std::move(samples), 0};
        ++replacedVoices_;
    } else {
        voices_.push_back({std::move(samples), 0});
        ++activeVoices_;
        peakVoices_ = std::max(peakVoices_, activeVoices_);
    }
}

void SoundMixer::render(float *output, size_t samples) {
    std::fill(output, output + samples, 0.f);
    for (auto &voice : voices_) {
        size_t count = std::min(samples, voice.samples.size() - voice.position);
        for (size_t i = 0; i < count; ++i)
            output[i] += voice.samples[voice.position + i];
        voice.position += count;
    }
    voices_.erase(std::remove_if(voices_.begin(), voices_.end(), [](const Voice &voice) {
                       return voice.position == voice.samples.size();
                   }),
                   voices_.end());
    activeVoices_ = voices_.size();
}

void SoundMixer::clear() {
    voices_.clear();
    activeVoices_ = 0;
}
} // namespace toy

#include "gameplay_tuning.hpp"
#include "sound_mixer.hpp"
#include <array>
#include <cmath>
#include <iostream>
#include <stdexcept>
void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() try {
    toy::SoundMixer mixed(1), first(1), second(1);
    std::array<float, 512> a{}, b{}, sum{};
    mixed.trigger(330, .1f);
    first.trigger(330, .1f);
    second.trigger(330, .1f);
    second.clear();
    mixed.render(sum.data(), 512);
    first.render(a.data(), 512);
    mixed.trigger(550, .1f);
    second.trigger(550, .1f);
    float difference = 0;
    for (int block = 0; block < 12; ++block) {
        mixed.render(sum.data(), 512);
        first.render(a.data(), 512);
        second.render(b.data(), 512);
        for (size_t i = 0; i < a.size(); ++i) {
            require(std::abs(sum[i] - (a[i] + b[i])) < .000001f,
                    "successive contacts did not overlap sample-for-sample");
            if (block == 0)
                difference += std::abs(sum[i] - a[i]);
        }
    }
    require(difference > 1, "second voice did not start during the first voice");
    for (float sample : sum)
        require(sample == 0, "completed voices did not return to silence");
    for (int i = 0; i < 100; ++i)
        mixed.trigger(220 + i, .15f);
    mixed.render(sum.data(), 512);
    for (float sample : sum)
        require(std::isfinite(sample) && std::abs(sample) <= 1,
                "dense contacts produced invalid audio");
    mixed.clear();
    mixed.render(sum.data(), 512);
    for (float sample : sum)
        require(sample == 0, "mute did not clear active voices");
    toy::SoundMixer capacity;
    for (int i = 0; i <= toy::tuning::maximumSimultaneousImpactVoices; ++i)
        capacity.trigger(330, .1f);
    require(capacity.activeVoices() == toy::tuning::maximumSimultaneousImpactVoices,
            "active voice count exceeded capacity");
    require(capacity.peakVoices() == toy::tuning::maximumSimultaneousImpactVoices,
            "peak voice count did not record capacity");
    require(capacity.replacedVoices() == 1, "voice replacement was not recorded at capacity");

    toy::SoundMixer slow(1), slowAlternate(0xdeadbeef), fast(1), fastAlternate(0xdeadbeef);
    std::array<float, toy::SoundMixer::duration> slowSamples{}, slowAlternateSamples{},
        fastSamples{}, fastAlternateSamples{};
    slow.trigger(440, .1f, 0.0f);
    slowAlternate.trigger(440, .1f, 0.0f);
    fast.trigger(440, .1f, 1.0f);
    fastAlternate.trigger(440, .1f, 1.0f);
    slow.render(slowSamples.data(), slowSamples.size());
    slowAlternate.render(slowAlternateSamples.data(), slowAlternateSamples.size());
    fast.render(fastSamples.data(), fastSamples.size());
    fastAlternate.render(fastAlternateSamples.data(), fastAlternateSamples.size());
    float slowDifference = 0.0f;
    float fastDifference = 0.0f;
    float slowTailEnergy = 0.0f;
    float fastTailEnergy = 0.0f;
    for (size_t i = 0; i < slowSamples.size(); ++i) {
        slowDifference += std::abs(slowSamples[i] - slowAlternateSamples[i]);
        fastDifference += std::abs(fastSamples[i] - fastAlternateSamples[i]);
        if (i >= 1000 && i < 2000) {
            slowTailEnergy += slowSamples[i] * slowSamples[i];
            fastTailEnergy += fastSamples[i] * fastSamples[i];
        }
    }
    require(slowDifference > .1f, "impact pitch did not vary between triggers");
    require(fastDifference > 1.0f, "impact phase and texture did not vary between triggers");
    require(fastTailEnergy < slowTailEnergy,
            "fast impact tone did not decay before slow impact tone");
    std::cout << "Audio mixing checks passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

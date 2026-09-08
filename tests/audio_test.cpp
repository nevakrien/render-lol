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
    toy::SoundMixer mixed, first, second;
    std::array<float, 512> a{}, b{}, sum{};
    mixed.trigger(330, .1f);
    first.trigger(330, .1f);
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
    std::cout << "Audio mixing checks passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

#include "gameplay_tuning.hpp"
#include "physics.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}

int main() try {
    require(toy::tuning::heatPower(0) < toy::tuning::heatPower(toy::tuning::heatPeak),
            "impact power did not rise toward peak heat");
    require(toy::tuning::heatPower(toy::tuning::heatPeak) >
                toy::tuning::heatPower(toy::tuning::heatPowerDeclineEnd),
            "impact power did not decline after peak heat");
    require(toy::tuning::heatPower(toy::tuning::heatBurnout) == 0,
            "impact power did not reach zero at burnout");

    toy::Physics physics;
    physics.reset(false);
    physics.spawn(toy::Shape::Circle, {0, -3.4f});
    require(physics.beginDrag({0, -3.4f}), "could not grab test shape");
    physics.moveDrag({0, -4.3f});

    int impacts = 0;
    bool wasOverheated = false;
    bool transitionHadImpact = false;
    float heatAfterOneSecond = 0;
    for (int i = 0; i < 720; ++i) {
        physics.step();
        auto hits = physics.takeImpacts();
        for (const auto &impact : hits)
            ++impacts;
        auto body = physics.snapshot().front();
        if (!wasOverheated && body.overheated)
            transitionHadImpact =
                transitionHadImpact ||
                std::any_of(hits.begin(), hits.end(), [](const auto &hit) { return hit.strength > 0; });
        wasOverheated = body.overheated;
        if (i == 119)
            heatAfterOneSecond = body.heat;
    }

    auto hot = physics.snapshot().front();
    require(impacts > 8, "forced wall contact did not generate repeated impacts");
    require(hot.overheated, "repeated wall contact did not overheat the shape");
    require(heatAfterOneSecond < toy::tuning::heatBurnout,
            "a single contact reached burnout within one second");
    require(hot.heat < toy::tuning::heatBurnout,
            "a single repeated pair reached complete burnout");
    require(transitionHadImpact, "the collision crossing into overheat was suppressed");

    physics.endDrag();
    float hotLevel = hot.heat;
    for (int i = 0; i < 600; ++i) {
        physics.step();
        physics.takeImpacts();
    }
    auto cooled = physics.snapshot().front();
    require(cooled.heat < hotLevel, "shape did not cool after forced contact ended");
    require(!cooled.overheated, "shape did not leave overheat after cooling");

    std::cout << "Physics heat checks passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

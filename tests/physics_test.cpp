#include "physics.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

void require(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() try {
    toy::Physics p;
    for (int scenario = 0; scenario < 3; ++scenario) {
        p.reset(false);
        p.spawn(toy::Shape::Rectangle, scenario != 0, {-2, 0}, {3, 0});
        p.spawn(toy::Shape::Rectangle, scenario == 2, {2, 0}, {-3, 0});
        bool impact = false;
        for (int i = 0; i < 720; ++i) {
            p.step();
            impact |= !p.takeImpacts().empty();
            require(p.healthy(), "collision scenario escaped board or became non-finite");
        }
        auto s = p.stats();
        require(scenario == 0   ? s.rigidContacts > 0
                : scenario == 1 ? s.softRigidContacts > 0
                                : s.softSoftContacts > 0,
                "missing required collision type");
        require(impact && p.score() > 0, "contacts produced no impact events");
        std::cout << "collision scenario " << scenario << ": " << s.rigidContacts << " rigid, "
                  << s.softRigidContacts << " mixed, " << s.softSoftContacts << " soft contacts\n";
    }
    for (bool soft : {false, true}) {
        p.reset(false);
        p.spawn(toy::Shape::Circle, soft, {0, 0});
        auto initial = p.snapshot()[0];
        require(initial.triangles.size() >= 3, "body exposed no render triangle data");
        require(!p.beginDrag({6, 3}), "empty-space pick succeeded");
        require(p.beginDrag({0, 0}), "body pick failed");
        p.moveDrag({3, 2});
        for (int i = 0; i < 180; ++i) {
            p.step();
            p.takeImpacts();
            require(p.healthy(), "dragging destabilized body");
        }
        require(p.snapshot()[0].center.x > 1.5f, "drag did not move body");
        p.endDrag();
        require(!p.dragging(), "release failed");
    }
    p.reset();
    p.setGravity(true);
    for (int i = 0; i < 3600; ++i) {
        p.step();
        p.takeImpacts();
        require(p.healthy(), "mixed world unstable under gravity");
    }
    p.reset();
    auto softBody = p.snapshot()[5];
    require(p.beginDrag(softBody.center), "soft-body stress pick failed");
    for (int i = 0; i < 1200; ++i) {
        p.moveDrag(i % 120 < 60 ? toy::Vec2{-7.5f, 4.f} : toy::Vec2{7.5f, -4.f});
        p.step();
        p.takeImpacts();
        require(p.healthy(), "rapid long-distance dragging destabilized soft body");
    }
    p.endDrag();
    require(p.stats().softRecoveries == 0, "soft body required numerical recovery while dragging");
    p.reset(false);
    require(p.snapshot().empty() && p.score() == 0, "reset did not clear state");
    std::cout << "Physics checks passed\n";
    return 0;
} catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
}

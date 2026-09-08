#pragma once
#include <cstdint>
#include <memory>
#include <vector>

namespace toy {
struct Vec2 {
    float x = 0, y = 0;
};
struct Color {
    float r, g, b, a = 1;
};
enum class Shape { Circle, Triangle, Rectangle };
struct BodyView {
    int id;
    Color color;
    Vec2 center;
    std::vector<Vec2> outline;
    std::vector<Vec2> triangles;
};
struct Impact {
    Vec2 point;
    Color color;
    float strength;
    int a, b;
    float shapeId = 0;
    float rotation = 0;
};
struct PhysicsStats {
    int rigidContacts = 0;
};

// No SDL or Vulkan here. World units and input are independent of screen size.
class Physics {
  public:
    static constexpr float halfWidth = 8, halfHeight = 4.5f, stepSize = 1.f / 120;
    Physics();
    ~Physics();
    Physics(const Physics &) = delete;
    Physics &operator=(const Physics &) = delete;
    void reset(bool populate = true);
    int spawn(Shape shape, Vec2 position, Vec2 velocity = {});
    void step();
    bool beginDrag(Vec2 point);
    void moveDrag(Vec2 point);
    void endDrag();
    bool dragging() const;
    void setGravity(bool enabled);
    std::vector<BodyView> snapshot() const;
    std::vector<Impact> takeImpacts();
    PhysicsStats stats() const;
    bool healthy() const;
    uint64_t score() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};
} // namespace toy

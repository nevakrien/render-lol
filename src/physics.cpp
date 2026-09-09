#include "physics.hpp"
#include "gameplay_tuning.hpp"
#include <box2d/box2d.h>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <set>
#include <vector>

namespace toy {
namespace {
constexpr float pi = 3.14159265359f;

const Color palette[] = {{.27f, .72f, 1},    {1, .43f, .32f}, {1, .78f, .25f},
                         {.45f, .88f, .57f}, {.73f, .52f, 1}, {1, .49f, .73f}};

bool contains(const std::vector<Vec2> &polygon, Vec2 point) {
    bool inside = false;
    for (size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        auto a = polygon[i], b = polygon[j];
        if ((a.y > point.y) != (b.y > point.y) &&
            point.x < (b.x - a.x) * (point.y - a.y) / (b.y - a.y) + a.x)
            inside = !inside;
    }
    return inside;
}

struct Object {
    int id = 0;
    Shape shape = Shape::Circle;
    Color color{};
    b2BodyId bodyId = b2_nullBodyId;
    b2ShapeId shapeId = b2_nullShapeId;
    float heat = 0;
    float collisionLoad = 0;
    float preStepSpeed = 0;
    float baseRestitution = 0;
    bool overheated = false;
};

struct PairState {
    float freshness = 1;
    float lastHit = -100;
};
} // namespace

struct Physics::Impl {
    struct Drag {
        int objectId;
        Vec2 target;
    };

    b2WorldId world = b2_nullWorldId;
    std::vector<Object> objects;
    std::vector<Object> walls;
    std::map<int, int> bodyToObject; // bodyId.index1 -> object id
    std::map<std::pair<int, int>, PairState> pairs;
    std::vector<Impact> impacts;
    PhysicsStats counters;
    int nextId = 1;
    std::map<DragId, Drag> drags;
    DragId nextDragId = 1;
    float time = 0;
    uint64_t points = 0;
    bool gravity = false;

    Impl() {
        b2WorldDef def = b2DefaultWorldDef();
        def.hitEventThreshold = 0.0f;
        world = b2CreateWorld(&def);
    }

    Object *find(int id) {
        for (auto &obj : objects)
            if (obj.id == id)
                return &obj;
        return nullptr;
    }

    int idFromBody(b2BodyId bodyId) const {
        auto it = bodyToObject.find(bodyId.index1);
        return it != bodyToObject.end() ? it->second : -1;
    }

    int idFromShape(b2ShapeId shapeId) const {
        b2BodyId bid = b2Shape_GetBody(shapeId);
        auto it = bodyToObject.find(bid.index1);
        if (it != bodyToObject.end())
            return it->second;
        for (auto &wall : walls)
            if (wall.bodyId.index1 == bid.index1)
                return wall.id;
        return -1;
    }

    Vec2 bodyPosition(b2BodyId id) const {
        b2Vec2 p = b2Body_GetPosition(id);
        return {p.x, p.y};
    }

    float bodyRotation(b2BodyId id) const {
        b2Rot r = b2Body_GetRotation(id);
        return atan2f(r.s, r.c);
    }

    std::vector<Vec2> circleOutline(b2Vec2 center, float radius, int segments = 24) const {
        std::vector<Vec2> result;
        result.reserve(segments);
        for (int i = 0; i < segments; ++i) {
            float a = float(i) / segments * 2.0f * pi;
            result.push_back({center.x + radius * cosf(a), center.y + radius * sinf(a)});
        }
        return result;
    }

    std::vector<Vec2> polygonOutline(b2BodyId bodyId, Shape shape) const {
        b2Vec2 center = b2Body_GetPosition(bodyId);
        float angle = b2Rot_GetAngle(b2Body_GetRotation(bodyId));
        float ca = cosf(angle), sa = sinf(angle);

        auto rotate = [&](float lx, float ly) -> Vec2 {
            return {center.x + ca * lx - sa * ly, center.y + sa * lx + ca * ly};
        };

        if (shape == Shape::Circle) {
            b2ShapeId sid = b2_nullShapeId;
            int count = b2Body_GetShapes(bodyId, &sid, 1);
            if (count > 0) {
                b2Circle circle = b2Shape_GetCircle(sid);
                return circleOutline(center, circle.radius);
            }
            return circleOutline(center, 0.8f);
        } else if (shape == Shape::Rectangle) {
            b2ShapeId sid = b2_nullShapeId;
            int count = b2Body_GetShapes(bodyId, &sid, 1);
            if (count > 0) {
                b2Polygon poly = b2Shape_GetPolygon(sid);
                std::vector<Vec2> result;
                for (int i = 0; i < poly.count; ++i)
                    result.push_back(rotate(poly.vertices[i].x, poly.vertices[i].y));
                return result;
            }
            float hx = 0.85f, hy = 0.65f;
            return {rotate(-hx, -hy), rotate(hx, -hy), rotate(hx, hy), rotate(-hx, hy)};
        } else {
            float r = 1.0f;
            std::vector<Vec2> result;
            for (int i = 0; i < 3; ++i) {
                float a = angle + float(i) * 2.0f * pi / 3.0f;
                result.push_back({center.x + r * cosf(a), center.y + r * sinf(a)});
            }
            return result;
        }
    }

    std::vector<Vec2> triangleMesh(b2BodyId bodyId, Shape shape) const {
        auto outline = polygonOutline(bodyId, shape);
        std::vector<Vec2> triangles;
        if (outline.size() >= 3) {
            Vec2 center{};
            for (auto &p : outline) {
                center.x += p.x;
                center.y += p.y;
            }
            center.x /= outline.size();
            center.y /= outline.size();
            for (size_t i = 0; i < outline.size(); ++i) {
                size_t j = (i + 1) % outline.size();
                triangles.push_back(center);
                triangles.push_back(outline[i]);
                triangles.push_back(outline[j]);
            }
        }
        return triangles;
    }

    void addWall(Vec2 position, Vec2 size, int id) {
        b2BodyDef bodyDef = b2DefaultBodyDef();
        bodyDef.type = b2_staticBody;
        bodyDef.position = {position.x, position.y};
        b2BodyId bid = b2CreateBody(world, &bodyDef);

        b2ShapeDef shapeDef = b2DefaultShapeDef();
        shapeDef.friction = 0.2f;
        shapeDef.restitution = 0.0f;
        shapeDef.enableHitEvents = true;
        b2Polygon box = b2MakeBox(size.x * 0.5f, size.y * 0.5f);
        b2ShapeId sid = b2CreatePolygonShape(bid, &shapeDef, &box);

        Object obj;
        obj.id = id;
        obj.shape = Shape::Rectangle;
        obj.color = palette[0];
        obj.bodyId = bid;
        obj.shapeId = sid;
        walls.push_back(obj);
    }

    void clear() {
        drags.clear();
        nextDragId = 1;
        if (b2World_IsValid(world))
            b2DestroyWorld(world);
        b2WorldDef def = b2DefaultWorldDef();
        def.hitEventThreshold = 0.0f;
        world = b2CreateWorld(&def);
        objects.clear();
        walls.clear();
        bodyToObject.clear();
        pairs.clear();
        impacts.clear();
        counters = {};
        nextId = 1;
        time = 0;
        points = 0;
    }
};

Physics::Physics() : impl(std::make_unique<Impl>()) { reset(); }
Physics::~Physics() = default;

void Physics::reset(bool populate) {
    impl->clear();
    setGravity(false);
    impl->addWall({-8.5f, 0}, {1, 11}, -1);
    impl->addWall({8.5f, 0}, {1, 11}, -2);
    impl->addWall({0, -5}, {16, 1}, -3);
    impl->addWall({0, 5}, {16, 1}, -4);
    if (populate) {
        spawn(Shape::Circle, {-5, 2}, {2, -1});
        spawn(Shape::Triangle, {-1.8f, 2}, {1.5f, -1.2f});
        spawn(Shape::Rectangle, {2, 2}, {-1, -1.8f});
        spawn(Shape::Circle, {5, -1.6f}, {-2, 1.2f});
        spawn(Shape::Triangle, {-4, -2}, {1.7f, 1});
        spawn(Shape::Rectangle, {.3f, -1.8f}, {-1.5f, 1.6f});
    }
}

int Physics::spawn(Shape shape, Vec2 position, Vec2 velocity) {
    if (impl->objects.size() >= 24)
        return 0;

    b2BodyDef bodyDef = b2DefaultBodyDef();
    bodyDef.type = b2_dynamicBody;
    bodyDef.position = {position.x, position.y};
    bodyDef.linearDamping = 0.0f;
    bodyDef.angularDamping = 0.0f;
    bodyDef.gravityScale = 1.0f;
    bodyDef.isAwake = true;
    bodyDef.isBullet = true;
    bodyDef.linearVelocity = {velocity.x, velocity.y};

    Object obj;
    obj.id = impl->nextId++;
    obj.shape = shape;
    obj.color = palette[(obj.id - 1) % 6];

    obj.bodyId = b2CreateBody(impl->world, &bodyDef);

    b2ShapeDef shapeDef = b2DefaultShapeDef();
    shapeDef.friction = 0.2f;
    shapeDef.restitution = shape == Shape::Circle ? 0.93f : 0.8f;
    obj.baseRestitution = shapeDef.restitution;
    shapeDef.density = 1.0f;
    shapeDef.enableHitEvents = true;

    if (shape == Shape::Circle) {
        b2Circle circle = {{0, 0}, 0.8f};
        obj.shapeId = b2CreateCircleShape(obj.bodyId, &shapeDef, &circle);
    } else if (shape == Shape::Rectangle) {
        b2Polygon rect = b2MakeBox(0.85f, 0.65f);
        obj.shapeId = b2CreatePolygonShape(obj.bodyId, &shapeDef, &rect);
    } else {
        float r = 1.0f;
        b2Vec2 verts[3];
        for (int i = 0; i < 3; ++i) {
            float a = float(i) * 2.0f * pi / 3.0f;
            verts[i] = {r * cosf(a), r * sinf(a)};
        }
        b2Hull hull = b2ComputeHull(verts, 3);
        b2Polygon poly = b2MakePolygon(&hull, 0.0f);
        obj.shapeId = b2CreatePolygonShape(obj.bodyId, &shapeDef, &poly);
    }

    int id = obj.id;
    impl->objects.push_back(obj);
    impl->bodyToObject[impl->objects.back().bodyId.index1] = id;
    return id;
}

void Physics::setGravity(bool enabled) {
    impl->gravity = enabled;
    b2World_SetGravity(impl->world, {0, enabled ? -9.8f : 0});
}

Physics::DragId Physics::beginDrag(Vec2 point) {
    for (auto it = impl->objects.rbegin(); it != impl->objects.rend(); ++it) {
        auto outline = impl->polygonOutline(it->bodyId, it->shape);
        if (!contains(outline, point))
            continue;
        DragId drag = impl->nextDragId++;
        if (drag == 0)
            drag = impl->nextDragId++;
        impl->drags.emplace(drag, Impl::Drag{it->id, point});
        b2Body_SetLinearDamping(it->bodyId, 100.0f);
        return drag;
    }
    return 0;
}

void Physics::moveDrag(DragId drag, Vec2 point) {
    auto it = impl->drags.find(drag);
    if (it != impl->drags.end())
        it->second.target = {std::clamp(point.x, -7.8f, 7.8f),
                             std::clamp(point.y, -4.3f, 4.3f)};
}

void Physics::endDrag(DragId drag) {
    auto it = impl->drags.find(drag);
    if (it == impl->drags.end())
        return;
    int objectId = it->second.objectId;
    impl->drags.erase(it);
    bool stillDragged = std::any_of(impl->drags.begin(), impl->drags.end(),
                                    [objectId](const auto &entry) {
                                        return entry.second.objectId == objectId;
                                    });
    if (!stillDragged)
        if (auto *object = impl->find(objectId))
            b2Body_SetLinearDamping(object->bodyId, 0.0f);
}

void Physics::endAllDrags() {
    std::set<int> objectIds;
    for (const auto &[drag, state] : impl->drags) {
        (void)drag;
        objectIds.insert(state.objectId);
    }
    for (int objectId : objectIds)
        if (auto *object = impl->find(objectId))
            b2Body_SetLinearDamping(object->bodyId, 0.0f);
    impl->drags.clear();
}

bool Physics::dragging() const { return !impl->drags.empty(); }

void Physics::step() {
    auto &state = *impl;
    state.time += stepSize;

    for (auto &object : state.objects)
        object.collisionLoad = 0;

    for (const auto &[drag, dragState] : state.drags) {
        (void)drag;
        auto *object = state.find(dragState.objectId);
        if (!object)
            continue;
        b2Vec2 bodyPos = b2Body_GetPosition(object->bodyId);
        b2Vec2 delta = {dragState.target.x - bodyPos.x, dragState.target.y - bodyPos.y};
        float scale = stepSize * 700.0f * (b2Body_GetMass(object->bodyId) + 0.7f);
        b2Body_ApplyLinearImpulseToCenter(object->bodyId, {delta.x * scale, delta.y * scale}, true);
    }

    for (auto &object : state.objects) {
        b2Vec2 velocity = b2Body_GetLinearVelocity(object.bodyId);
        object.preStepSpeed = std::sqrt(velocity.x * velocity.x + velocity.y * velocity.y);
    }

    b2World_Step(state.world, stepSize, 4);

    b2ContactEvents events = b2World_GetContactEvents(state.world);
    for (int i = 0; i < events.hitCount; ++i) {
        auto &hit = events.hitEvents[i];
        int idA = state.idFromShape(hit.shapeIdA);
        int idB = state.idFromShape(hit.shapeIdB);

        if (idA < 0 && idB < 0)
            continue;
        if (idA == idB)
            continue;

        auto key = std::minmax(idA, idB);
        float physicalStrength = hit.approachSpeed / tuning::fullImpactSpeed;
        physicalStrength = std::clamp(physicalStrength, 0.0f, 1.0f);

        auto &pair = state.pairs[{key.first, key.second}];
        float elapsed = state.time - pair.lastHit;
        pair.freshness =
            std::min(1.0f, pair.freshness + elapsed / tuning::pairFreshnessRecoverySeconds);
        float freshness = pair.freshness;
        pair.freshness *= tuning::pairFreshnessDecay;
        pair.lastHit = state.time;

        Object *objectA = state.find(idA);
        Object *objectB = state.find(idB);
        float heatMultiplier = 1.0f;
        if (objectA && objectB)
            heatMultiplier =
                std::min(tuning::heatPower(objectA->heat), tuning::heatPower(objectB->heat));
        else if (objectA)
            heatMultiplier = tuning::heatPower(objectA->heat);
        else if (objectB)
            heatMultiplier = tuning::heatPower(objectB->heat);

        float outputMultiplier = freshness * heatMultiplier;
        float strength = physicalStrength * outputMultiplier;
        float displayHeat = 0;
        for (Object *object : {objectA, objectB}) {
            if (!object)
                continue;
            displayHeat = std::max(displayHeat, object->heat);
        }
        float effectiveMass = 1.0f;
        if (objectA && objectB) {
            float massA = b2Body_GetMass(objectA->bodyId);
            float massB = b2Body_GetMass(objectB->bodyId);
            effectiveMass = massA * massB / (massA + massB);
        } else if (Object *object = objectA ? objectA : objectB) {
            effectiveMass = b2Body_GetMass(object->bodyId);
        }

        {
            float seed = std::fmod(std::abs(std::sin(state.time * 91.7f + hit.point.x * 17.3f +
                                                     hit.point.y * 37.1f)),
                                   1.0f);
            const Object *owner = objectA ? objectA : objectB;
            if (objectA && objectB) {
                const Object *first = objectA->id < objectB->id ? objectA : objectB;
                const Object *second = first == objectA ? objectB : objectA;
                float totalSpeed = first->preStepSpeed + second->preStepSpeed;
                float chanceFirst = totalSpeed > 0 ? first->preStepSpeed / totalSpeed : 0.5f;
                float ownerRoll = std::fmod(
                    std::abs(std::sin(state.time * 53.1f + hit.point.x * 79.3f +
                                      hit.point.y * 31.7f + key.first * 11.3f +
                                      key.second * 7.1f) *
                             43758.5453f),
                    1.0f);
                owner = ownerRoll < chanceFirst ? first : second;
            }
            state.impacts.push_back(
                {{hit.point.x, hit.point.y}, owner ? owner->color : palette[0], strength,
                   key.first, key.second, owner ? float(owner->shape) : 0.0f,
                    owner ? state.bodyRotation(owner->bodyId) : 0.0f, seed,
                    displayHeat, freshness, effectiveMass, physicalStrength});
            state.points += uint64_t(
                (1.0f + physicalStrength * tuning::fullImpactPointBonus) * outputMultiplier);
        }

        // State changes happen after emission so the collision that crosses a
        // heat boundary still lands at its pre-impact power.
        float collisionLoad = tuning::collisionLoadBase +
                              physicalStrength * tuning::collisionLoadFromStrength +
                              (1.0f - freshness) * tuning::collisionLoadFromStaleness;
        if (objectA)
            objectA->collisionLoad += collisionLoad;
        if (objectB)
            objectB->collisionLoad += collisionLoad;
        ++state.counters.rigidContacts;
    }

    float blend = 1.0f - std::exp(-stepSize / tuning::heatSmoothingSeconds);
    for (auto &object : state.objects) {
        object.heat += (object.collisionLoad - object.heat) * blend;
        object.heat = std::clamp(object.heat, 0.0f, tuning::heatMaximum);
        object.overheated = object.heat >= tuning::heatPeak;
        float restitution;
        if (object.heat <= tuning::heatPeak) {
            float hot = object.heat / tuning::heatPeak;
            restitution = object.baseRestitution +
                          (tuning::peakRestitution - object.baseRestitution) * hot;
        } else {
            float damaged = std::clamp((object.heat - tuning::heatPeak) /
                                           tuning::heatVisualDamageRange,
                                       0.0f, 1.0f);
            restitution = tuning::peakRestitution +
                          (tuning::burnoutRestitution - tuning::peakRestitution) * damaged;
        }
        b2Shape_SetRestitution(object.shapeId, restitution);
    }
}

std::vector<BodyView> Physics::snapshot() const {
    std::vector<BodyView> result;
    result.reserve(impl->objects.size());
    for (auto &obj : impl->objects) {
        if (!b2Body_IsValid(obj.bodyId))
            continue;
        auto outline = impl->polygonOutline(obj.bodyId, obj.shape);
        auto triangles = impl->triangleMesh(obj.bodyId, obj.shape);
        Vec2 center = impl->bodyPosition(obj.bodyId);
        result.push_back({obj.id, obj.color, center, std::move(outline), std::move(triangles),
                          obj.heat, impl->time, obj.overheated});
    }
    return result;
}

std::vector<Impact> Physics::takeImpacts() {
    std::vector<Impact> result;
    result.swap(impl->impacts);
    return result;
}

PhysicsStats Physics::stats() const { return impl->counters; }
uint64_t Physics::score() const { return impl->points; }
void Physics::setScore(uint64_t score) { impl->points = score; }

bool Physics::healthy() const {
    for (auto &obj : impl->objects) {
        if (!b2Body_IsValid(obj.bodyId))
            return false;
        Vec2 pos = impl->bodyPosition(obj.bodyId);
        if (!std::isfinite(pos.x) || !std::isfinite(pos.y) || std::abs(pos.x) > 8.5f ||
            std::abs(pos.y) > 5.f)
            return false;
    }
    return true;
}
} // namespace toy

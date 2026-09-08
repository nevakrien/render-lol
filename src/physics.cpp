#include "physics.hpp"
#include <qmesh.h>
#include <qrigidbody.h>
#include <qsoftbody.h>
#include <qworld.h>
#include <algorithm>
#include <cmath>
#include <map>
#include <set>
#include <unordered_map>

namespace toy {
namespace {
constexpr float pi = 3.14159265359f;
constexpr float scale = 64.f;

QVector q(Vec2 v) { return {v.x * scale, v.y * scale}; }
Vec2 v(QVector p) { return {p.x / scale, p.y / scale}; }

const Color palette[] = {{.27f, .72f, 1},    {1, .43f, .32f}, {1, .78f, .25f},
                         {.45f, .88f, .57f}, {.73f, .52f, 1}, {1, .49f, .73f}};

QMesh *makeMesh(Shape shape, bool soft) {
    if (shape == Shape::Rectangle)
        return QMesh::CreateWithRect({1.7f * scale, 1.3f * scale}, QVector::Zero(),
                                     soft ? QVector(4, 3) : QVector::Zero(), soft, true, 1.f);
    int sides = shape == Shape::Circle ? 24 : 3;
    float radius = (shape == Shape::Circle ? .8f : 1.f) * scale;
    return QMesh::CreateWithPolygon(radius, sides, QVector::Zero(), soft ? 2 : -1, soft, true,
                                    1.f);
}

float cross(Vec2 a, Vec2 b) { return a.x * b.y - a.y * b.x; }

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
} // namespace

struct Physics::Impl {
    struct Object {
        int id = 0;
        bool soft = false;
        Color color{};
        QBody *body = nullptr;
        QMesh *mesh = nullptr;
    };
    struct Contact {
        Vec2 point{};
        Vec2 normal{};
        float strength = 0;
    };

    QWorld world;
    std::vector<std::unique_ptr<Object>> objects;
    std::vector<std::unique_ptr<Object>> walls;
    std::unordered_map<QBody *, Object *> owners;
    std::map<std::pair<int, int>, Contact> contacts;
    std::set<std::pair<int, int>> previousContacts;
    std::map<std::pair<int, int>, float> lastImpact;
    std::vector<Impact> impacts;
    PhysicsStats counters;
    int nextId = 1;
    int dragId = 0;
    int dragParticle = 0;
    Vec2 target{};
    Vec2 localGrab{};
    float time = 0;
    uint64_t points = 0;
    bool gravity = false;

    Impl() { world.SetSleepingEnabled(false)->SetIterationCount(8); }

    Object *find(int id) const {
        for (auto &object : objects)
            if (object->id == id)
                return object.get();
        return nullptr;
    }

    std::vector<Vec2> outline(const Object &object) const {
        std::vector<Vec2> result;
        result.reserve(object.mesh->GetPolygonParticleCount());
        for (int i = 0; i < object.mesh->GetPolygonParticleCount(); ++i)
            result.push_back(v(object.mesh->GetParticleFromPolygon(i)->GetGlobalPosition()));
        return result;
    }

    Vec2 velocity(const Object &object) const {
        QVector result = QVector::Zero();
        if (object.soft) {
            int count = object.mesh->GetParticleCount();
            for (int i = 0; i < count; ++i) {
                auto *particle = object.mesh->GetParticleAt(i);
                result += particle->GetGlobalPosition() - particle->GetPreviousGlobalPosition();
            }
            if (count)
                result /= float(count);
        } else {
            result = object.body->GetPosition() - object.body->GetPreviousPosition();
        }
        return {result.x / (scale * stepSize), result.y / (scale * stepSize)};
    }

    void listen(Object &object) {
        object.body->CollisionEventListener = [this](QBody *body, QBody::CollisionInfo info) {
            auto owner = owners.find(body);
            auto other = owners.find(info.body);
            if (owner == owners.end() || other == owners.end() || owner->second->id == other->second->id)
                return true;

            auto key = std::minmax(owner->second->id, other->second->id);
            Vec2 a = velocity(*owner->second);
            Vec2 b = velocity(*other->second);
            Vec2 normal = v(info.normal * scale);
            float strength = std::abs((a.x - b.x) * normal.x + (a.y - b.y) * normal.y);
            auto existing = contacts.find(key);
            if (existing == contacts.end() || strength > existing->second.strength)
                contacts[key] = {v(info.position), normal, strength};
            return true;
        };
    }

    void addWall(Vec2 position, Vec2 size, int id) {
        auto object = std::make_unique<Object>();
        object->id = id;
        object->body = new QRigidBody();
        object->mesh = QMesh::CreateWithRect(q(size));
        object->body->AddMesh(object->mesh)->SetPosition(q(position));
        object->body->SetMode(QBody::STATIC)->SetRestitution(.85f)->SetFriction(.2f);
        owners[object->body] = object.get();
        listen(*object);
        world.AddBody(object->body);
        walls.push_back(std::move(object));
    }

    void clear() {
        dragId = 0;
        world.ClearWorld();
        objects.clear();
        walls.clear();
        owners.clear();
        contacts.clear();
        previousContacts.clear();
        lastImpact.clear();
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
        spawn(Shape::Circle, false, {-5, 2}, {2, -1});
        spawn(Shape::Triangle, true, {-1.8f, 2}, {1.5f, -1.2f});
        spawn(Shape::Rectangle, false, {2, 2}, {-1, -1.8f});
        spawn(Shape::Circle, true, {5, -1.6f}, {-2, 1.2f});
        spawn(Shape::Triangle, false, {-4, -2}, {1.7f, 1});
        spawn(Shape::Rectangle, true, {.3f, -1.8f}, {-1.5f, 1.6f});
    }
}

int Physics::spawn(Shape shape, bool soft, Vec2 position, Vec2 velocity) {
    if (impl->objects.size() >= 24)
        return 0;

    auto object = std::make_unique<Impl::Object>();
    object->id = impl->nextId++;
    object->soft = soft;
    object->color = palette[(object->id - 1) % 6];
    object->mesh = makeMesh(shape, soft);
    if (soft) {
        auto *body = new QSoftBody();
        body
               ->SetRigidity(.72f)
            ->SetAreaPreservingEnabled(true)
            ->SetAreaPreservingRate(.8f)
        //     ->SetAreaPreservingRigidity(.8f)
            // ->SetShapeMatchingEnabled(true)
        //     ->SetShapeMatchingRate(.12f)
        //     ->SetParticleSpesificMassEnabled(true)
        //     ->SetParticleSpesificMass(.08f)
            ;
        object->body = body;
    } else {
        object->body = new QRigidBody();
    }
    object->body->AddMesh(object->mesh)
        ->SetPosition(q(position))
        ->SetMass(1.5f)
        ->SetRestitution(.85f)
        ->SetFriction(.2f)
        ->SetAirFriction(.002f)
        ->SetVelocityLimit(15.f * scale * stepSize);

    QVector perStepVelocity = q(velocity) * stepSize;
    if (soft) {
        for (int i = 0; i < object->mesh->GetParticleCount(); ++i) {
            auto *particle = object->mesh->GetParticleAt(i);
            particle->SetPreviousGlobalPosition(particle->GetGlobalPosition() - perStepVelocity);
        }
    } else {
        object->body->SetPreviousPosition(object->body->GetPosition() - perStepVelocity);
    }

    int id = object->id;
    impl->owners[object->body] = object.get();
    impl->listen(*object);
    impl->world.AddBody(object->body);
    impl->objects.push_back(std::move(object));
    return id;
}

void Physics::setGravity(bool enabled) {
    impl->gravity = enabled;
    impl->world.SetGravity({0, enabled ? -4.f * scale * stepSize * stepSize : 0});
}

bool Physics::beginDrag(Vec2 point) {
    endDrag();
    for (auto it = impl->objects.rbegin(); it != impl->objects.rend(); ++it) {
        auto &object = **it;
        if (!contains(impl->outline(object), point))
            continue;
        impl->dragId = object.id;
        moveDrag(point);
        // if (object.soft) {
        //     float best = 1e9f;
        //     for (int i = 0; i < object.mesh->GetParticleCount(); ++i) {
        //         Vec2 p = v(object.mesh->GetParticleAt(i)->GetGlobalPosition());
        //         float dx = p.x - point.x, dy = p.y - point.y;
        //         if (dx * dx + dy * dy < best) {
        //             best = dx * dx + dy * dy;
        //             impl->dragParticle = i;
        //         }
        //     }
        // } else {
            Vec2 center = v(object.body->GetPosition());
            float angle = -object.body->GetRotation();
            Vec2 offset{point.x - center.x, point.y - center.y};
            impl->localGrab = {offset.x * std::cos(angle) - offset.y * std::sin(angle),
                               offset.x * std::sin(angle) + offset.y * std::cos(angle)};
        // }
        return true;
    }
    return false;
}

void Physics::moveDrag(Vec2 point) {
    impl->target = {std::clamp(point.x, -7.8f, 7.8f), std::clamp(point.y, -4.3f, 4.3f)};
}

void Physics::endDrag() { impl->dragId = 0; }
bool Physics::dragging() const { return impl->dragId != 0; }

void Physics::step() {
    auto &state = *impl;
    state.time += stepSize;
    state.contacts.clear();

    if (auto *object = state.find(state.dragId)) {
        if (object->soft) {
            auto *particle = object->mesh->GetParticleAt(state.dragParticle);
            QVector delta = q(state.target) - particle->GetGlobalPosition();
            QVector velocity = particle->GetGlobalPosition() - particle->GetPreviousGlobalPosition();
            particle->ApplyForce(delta * .12f - velocity * .3f);
        } else {
            float angle = object->body->GetRotation();
            Vec2 arm{state.localGrab.x * std::cos(angle) - state.localGrab.y * std::sin(angle),
                     state.localGrab.x * std::sin(angle) + state.localGrab.y * std::cos(angle)};
            QVector grab = object->body->GetPosition() + q(arm);
            QVector delta = q(state.target) - grab;
            QVector velocity = object->body->GetPosition() - object->body->GetPreviousPosition();
            static_cast<QRigidBody *>(object->body)->ApplyForce(delta * .08f - velocity * .25f,
                                                               q(arm));
        }
    }

    state.world.Update();

    std::set<std::pair<int, int>> now;
    for (auto &[key, contact] : state.contacts) {
        now.insert(key);
        auto *a = state.find(key.first);
        auto *b = state.find(key.second);
        bool softA = a && a->soft;
        bool softB = b && b->soft;
        if (softA && softB)
            ++state.counters.softSoftContacts;
        else if (softA || softB)
            ++state.counters.softRigidContacts;
        else
            ++state.counters.rigidContacts;

        if (contact.strength > .65f && !state.previousContacts.count(key) &&
            (!state.lastImpact.count(key) || state.time - state.lastImpact[key] > .12f)) {
            auto *owner = a ? a : b;
            state.impacts.push_back(
                {contact.point, owner ? owner->color : palette[0], contact.strength, key.first,
                 key.second});
            state.points += uint64_t(contact.strength * 10);
            state.lastImpact[key] = state.time;
        }
    }
    state.previousContacts = std::move(now);
}

std::vector<BodyView> Physics::snapshot() const {
    std::vector<BodyView> result;
    result.reserve(impl->objects.size());
    for (auto &object : impl->objects) {
        auto outline = impl->outline(*object);
        std::vector<Vec2> triangles;
        for (int i = 0; i < object->mesh->GetUVMapCount(); ++i) {
            auto face = object->mesh->GetUVMapAt(i);
            for (size_t j = 1; j + 1 < face.size(); ++j) {
                triangles.push_back(v(object->mesh->GetParticleAt(face[0])->GetGlobalPosition()));
                triangles.push_back(v(object->mesh->GetParticleAt(face[j])->GetGlobalPosition()));
                triangles.push_back(v(object->mesh->GetParticleAt(face[j + 1])->GetGlobalPosition()));
            }
        }
        Vec2 center{};
        for (auto point : outline) {
            center.x += point.x;
            center.y += point.y;
        }
        if (!outline.empty()) {
            center.x /= outline.size();
            center.y /= outline.size();
        }
        result.push_back({object->id, object->soft, object->color, center, std::move(outline),
                          std::move(triangles)});
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

bool Physics::healthy() const {
    for (auto &object : impl->objects)
        for (auto point : impl->outline(*object))
            if (!std::isfinite(point.x) || !std::isfinite(point.y) || std::abs(point.x) > 8.5f ||
                std::abs(point.y) > 5.f)
                return false;
    return true;
}
} // namespace toy

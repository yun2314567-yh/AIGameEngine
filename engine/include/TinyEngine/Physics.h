#pragma once

#include "TinyEngine/Math.h"
#include "TinyEngine/Scene.h"

#include <cstdint>
#include <memory>
#include <unordered_map>
#include <string>

namespace TinyEngine {

struct PhysicsBody {
    Entity EntityHandle{};
    BodyType Type = BodyType::Dynamic;
    float Mass = 1.0f;
    Vec3 Velocity{};
};

// Pure thread-safe component validation; false reports an error.
bool ValidateRigidBody(const RigidBodyComponent& component, std::string* error = nullptr);
bool ValidateBoxCollider(const BoxColliderComponent& component, std::string* error = nullptr);

class PhysicsWorld {
public:
    // All world operations run on its owning update thread. Invalid body input
    // returns null without changing the registered body.
    PhysicsWorld();
    ~PhysicsWorld();
    PhysicsWorld(const PhysicsWorld&) = delete;
    PhysicsWorld& operator=(const PhysicsWorld&) = delete;
    PhysicsBody* addBody(Entity entity, const RigidBodyComponent& component);
    bool removeBody(Entity entity);
    PhysicsBody* getBody(Entity entity);
    const PhysicsBody* getBody(Entity entity) const;
    // Scene components are authoritative. Invalid time/gravity is a no-op;
    // invalid bodies/transforms are skipped until corrected.
    void step(Scene& scene, float deltaSeconds, const Vec3& gravity = {0.0f, -9.81f, 0.0f});
    void clear();
    [[nodiscard]] bool hasCollisionBackend() const;

private:
    std::unordered_map<Entity, PhysicsBody, EntityHash> m_Bodies;
    struct Backend;
    std::unique_ptr<Backend> m_Backend;
};

} // namespace TinyEngine

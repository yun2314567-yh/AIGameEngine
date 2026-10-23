#include "TinyEngine/Physics.h"

#include <cmath>

#ifdef TINYENGINE_WITH_BULLET
#include <btBulletDynamicsCommon.h>
#endif

namespace TinyEngine {
namespace {
bool finite(const Vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
bool validTransform(const TransformComponent& transform) {
    const auto& q = transform.Rotation;
    const double norm = static_cast<double>(q.x) * q.x + static_cast<double>(q.y) * q.y +
        static_cast<double>(q.z) * q.z + static_cast<double>(q.w) * q.w;
    return finite(transform.Translation) && finite(transform.Scale) && norm > 0 && std::isfinite(norm) &&
        transform.Scale.x != 0 && transform.Scale.y != 0 && transform.Scale.z != 0;
}
} // namespace

bool ValidateRigidBody(const RigidBodyComponent& component, std::string* error) {
    if (static_cast<unsigned>(component.Type) > static_cast<unsigned>(BodyType::Kinematic) ||
        !std::isfinite(component.Mass) || component.Mass <= 0 || !finite(component.Velocity)) {
        if (error) *error = "Rigid body requires a valid type, positive finite mass and finite velocity";
        return false;
    }
    return true;
}

bool ValidateBoxCollider(const BoxColliderComponent& component, std::string* error) {
    if (!finite(component.HalfExtents) || component.HalfExtents.x <= 0 || component.HalfExtents.y <= 0 || component.HalfExtents.z <= 0 ||
        !std::isfinite(component.Friction) || component.Friction < 0 ||
        !std::isfinite(component.Restitution) || component.Restitution < 0 || component.Restitution > 1) {
        if (error) *error = "Box collider requires positive finite extents, nonnegative friction and restitution in [0,1]";
        return false;
    }
    return true;
}

struct PhysicsWorld::Backend {
#ifdef TINYENGINE_WITH_BULLET
    struct Body {
        std::unique_ptr<btCollisionShape> Shape;
        std::unique_ptr<btDefaultMotionState> Motion;
        std::unique_ptr<btRigidBody> RigidBody;
        Vec3 HalfExtents{};
        BodyType Type = BodyType::Static;
        float Mass = 0.0f;
    };
    btDefaultCollisionConfiguration Configuration;
    btCollisionDispatcher Dispatcher{&Configuration};
    btDbvtBroadphase Broadphase;
    btSequentialImpulseConstraintSolver Solver;
    btDiscreteDynamicsWorld World{&Dispatcher, &Broadphase, &Solver, &Configuration};
    std::unordered_map<Entity, Body, EntityHash> Bodies;

    ~Backend() {
        for (auto& [entity, body] : Bodies) World.removeRigidBody(body.RigidBody.get());
    }
    void remove(Entity entity) {
        const auto iterator = Bodies.find(entity);
        if (iterator == Bodies.end()) return;
        World.removeRigidBody(iterator->second.RigidBody.get());
        Bodies.erase(iterator);
    }
#endif
};

PhysicsWorld::PhysicsWorld() : m_Backend(std::make_unique<Backend>()) {}
PhysicsWorld::~PhysicsWorld() = default;

bool PhysicsWorld::hasCollisionBackend() const {
#ifdef TINYENGINE_WITH_BULLET
    return true;
#else
    return false;
#endif
}

void PhysicsWorld::clear() {
    m_Bodies.clear();
    m_Backend = std::make_unique<Backend>();
}

PhysicsBody* PhysicsWorld::addBody(Entity entity, const RigidBodyComponent& component) {
    if (entity.id == 0 || entity.generation == 0 || !ValidateRigidBody(component)) return nullptr;
    auto [iterator, inserted] = m_Bodies.insert_or_assign(entity, PhysicsBody{entity, component.Type, component.Mass, component.Velocity});
    return &iterator->second;
}

bool PhysicsWorld::removeBody(Entity entity) {
#ifdef TINYENGINE_WITH_BULLET
    m_Backend->remove(entity);
#endif
    return m_Bodies.erase(entity) != 0;
}

PhysicsBody* PhysicsWorld::getBody(Entity entity) {
    const auto iterator = m_Bodies.find(entity);
    return iterator == m_Bodies.end() ? nullptr : &iterator->second;
}

const PhysicsBody* PhysicsWorld::getBody(Entity entity) const {
    const auto iterator = m_Bodies.find(entity);
    return iterator == m_Bodies.end() ? nullptr : &iterator->second;
}

void PhysicsWorld::step(Scene& scene, float deltaSeconds, const Vec3& gravity) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f || !finite(gravity)) return;
#ifdef TINYENGINE_WITH_BULLET
    m_Backend->World.setGravity(btVector3(gravity.x, gravity.y, gravity.z));
    for (auto iterator = m_Bodies.begin(); iterator != m_Bodies.end();) {
        const Entity entity = iterator->first;
        auto* transform = scene.registry().tryGet<TransformComponent>(entity);
        auto* component = scene.registry().tryGet<RigidBodyComponent>(entity);
        if (!scene.valid(entity) || !transform || !component) {
            m_Backend->remove(entity);
            iterator = m_Bodies.erase(iterator);
            continue;
        }
        PhysicsBody& body = iterator->second;
        body.Type = component->Type;
        body.Mass = component->Mass;
        body.Velocity = component->Velocity;
        const auto* collider = scene.registry().tryGet<BoxColliderComponent>(entity);
        if (!ValidateRigidBody(*component) || !validTransform(*transform) || (collider && !ValidateBoxCollider(*collider))) {
            m_Backend->remove(entity);
            ++iterator;
            continue;
        }
        const Vec3 sourceExtents = collider ? collider->HalfExtents : Vec3{0.5f, 0.5f, 0.5f};
        const Vec3 halfExtents{std::fabs(sourceExtents.x * transform->Scale.x), std::fabs(sourceExtents.y * transform->Scale.y), std::fabs(sourceExtents.z * transform->Scale.z)};
        if (!finite(halfExtents) || halfExtents.x <= 0 || halfExtents.y <= 0 || halfExtents.z <= 0) {
            m_Backend->remove(entity); ++iterator; continue;
        }
        if (body.Type == BodyType::Kinematic) transform->Translation += body.Velocity * deltaSeconds;
        auto backendIterator = m_Backend->Bodies.find(entity);
        if (backendIterator != m_Backend->Bodies.end()
            && (backendIterator->second.Type != body.Type || backendIterator->second.Mass != body.Mass || !nearlyEqual(backendIterator->second.HalfExtents, halfExtents))) {
            m_Backend->remove(entity);
            backendIterator = m_Backend->Bodies.end();
        }
        btTransform pose;
        pose.setIdentity();
        pose.setOrigin(btVector3(transform->Translation.x, transform->Translation.y, transform->Translation.z));
        auto rotation = btQuaternion(transform->Rotation.x, transform->Rotation.y, transform->Rotation.z, transform->Rotation.w);
        rotation.normalize();
        pose.setRotation(rotation);
        if (backendIterator == m_Backend->Bodies.end()) {
            Backend::Body backend;
            backend.HalfExtents = halfExtents;
            backend.Type = body.Type;
            backend.Mass = body.Mass;
            backend.Shape = std::make_unique<btBoxShape>(btVector3(halfExtents.x, halfExtents.y, halfExtents.z));
            backend.Motion = std::make_unique<btDefaultMotionState>(pose);
            const btScalar mass = body.Type == BodyType::Dynamic ? body.Mass : 0.0f;
            btVector3 inertia(0, 0, 0);
            if (mass > 0.0f) backend.Shape->calculateLocalInertia(mass, inertia);
            btRigidBody::btRigidBodyConstructionInfo info(mass, backend.Motion.get(), backend.Shape.get(), inertia);
            backend.RigidBody = std::make_unique<btRigidBody>(info);
            if (body.Type == BodyType::Kinematic) {
                backend.RigidBody->setCollisionFlags(backend.RigidBody->getCollisionFlags() | btCollisionObject::CF_KINEMATIC_OBJECT);
                backend.RigidBody->setActivationState(DISABLE_DEACTIVATION);
            }
            auto [created, inserted] = m_Backend->Bodies.emplace(entity, std::move(backend));
            backendIterator = created;
            m_Backend->World.addRigidBody(created->second.RigidBody.get());
        }
        btRigidBody& native = *backendIterator->second.RigidBody;
        native.setWorldTransform(pose);
        native.getMotionState()->setWorldTransform(pose);
        native.setLinearVelocity(btVector3(body.Velocity.x, body.Velocity.y, body.Velocity.z));
        native.setGravity(component->Gravity ? btVector3(gravity.x, gravity.y, gravity.z) : btVector3(0, 0, 0));
        native.setFriction(collider ? collider->Friction : 0.5f);
        native.setRestitution(collider ? collider->Restitution : 0.0f);
        native.activate(true);
        ++iterator;
    }
    m_Backend->World.stepSimulation(deltaSeconds, 0);
    for (auto& [entity, backend] : m_Backend->Bodies) {
        auto* transform = scene.registry().tryGet<TransformComponent>(entity);
        auto* component = scene.registry().tryGet<RigidBodyComponent>(entity);
        if (!transform || !component) continue;
        btTransform pose;
        backend.RigidBody->getMotionState()->getWorldTransform(pose);
        const auto& position = pose.getOrigin();
        const auto& rotation = pose.getRotation();
        const auto& velocity = backend.RigidBody->getLinearVelocity();
        transform->Translation = {position.x(), position.y(), position.z()};
        transform->Rotation = {rotation.x(), rotation.y(), rotation.z(), rotation.w()};
        component->Velocity = {velocity.x(), velocity.y(), velocity.z()};
        m_Bodies.at(entity).Velocity = component->Velocity;
    }
#else
    for (auto iterator = m_Bodies.begin(); iterator != m_Bodies.end();) {
        const Entity entity = iterator->first;
        PhysicsBody& body = iterator->second;
        if (!scene.valid(entity)) {
            iterator = m_Bodies.erase(iterator);
            continue;
        }
        auto* transform = scene.registry().tryGet<TransformComponent>(entity);
        auto* rigidBody = scene.registry().tryGet<RigidBodyComponent>(entity);
        if (!transform || !rigidBody) {
            iterator = m_Bodies.erase(iterator);
            continue;
        }
        if (!ValidateRigidBody(*rigidBody) || !validTransform(*transform)) { ++iterator; continue; }
        body.Type = rigidBody->Type;
        body.Mass = rigidBody->Mass;
        body.Velocity = rigidBody->Velocity;
        if (body.Type == BodyType::Static) {
            ++iterator;
            continue;
        }
        if (body.Type == BodyType::Dynamic && rigidBody->Gravity) body.Velocity += gravity * deltaSeconds;
        transform->Translation += body.Velocity * deltaSeconds;
        rigidBody->Velocity = body.Velocity;
        ++iterator;
    }
#endif
}

} // namespace TinyEngine

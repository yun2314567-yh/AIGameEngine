#pragma once

#include "TinyEngine/Math.h"

#include <cstdint>

namespace TinyEngine {

enum class BodyType : std::uint8_t { Static, Dynamic, Kinematic };

struct RigidBodyComponent {
    BodyType Type = BodyType::Dynamic;
    float Mass = 1.0f;
    Vec3 Velocity{};
    bool Gravity = true;
};

struct BoxColliderComponent {
    Vec3 HalfExtents{0.5f, 0.5f, 0.5f};
    float Restitution = 0.0f;
    float Friction = 0.5f;
};

} // namespace TinyEngine

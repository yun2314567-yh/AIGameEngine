#pragma once

#include "TinyEngine/Math.h"

#include <string>

namespace TinyEngine {

struct MeshRendererComponent {
    std::string Mesh;
    std::string Material;
    bool Visible = true;
    bool CastShadows = true;
    bool ReceiveShadows = true;
};

enum class LightType { Directional, Point, Spot };

struct LightComponent {
    LightType Type = LightType::Point;
    Vec3 Color{1.0f, 1.0f, 1.0f};
    float Intensity = 1.0f;
    float Range = 10.0f;
    float InnerConeRadians = 0.4f;
    float OuterConeRadians = 0.8f;
    bool CastShadows = true;
};

// Thread-safe value validation; returns false and an error for unsupported or
// non-finite values. Does not modify the component.
[[nodiscard]] bool ValidateLight(const LightComponent& light, std::string* error = nullptr);

struct AudioSourceComponent {
    std::string Clip;
    float Volume = 1.0f;
    float Pitch = 1.0f;
    bool Loop = false;
    bool PlayOnStart = false;
};

} // namespace TinyEngine

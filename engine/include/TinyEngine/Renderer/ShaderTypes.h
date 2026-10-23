#pragma once

#include "TinyEngine/Math.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace TinyEngine {

// GPU upload types are plain, externally synchronized data. Matrices retain
// Mat4's column-major representation and shaders use mul(matrix, vector).
// Uploaded projections use right-handed view space (-Z forward), [0,1] depth,
// and positive clip Y. GraphicsContext removes the public helper's Y flip
// because NVRHI uses a negative-height Vulkan viewport.
struct alignas(16) GpuFloat4 {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    float W = 0.0f;
};

struct alignas(16) GpuUint4 {
    std::uint32_t X = 0;
    std::uint32_t Y = 0;
    std::uint32_t Z = 0;
    std::uint32_t W = 0;
};

inline constexpr std::uint32_t GpuMaxAdditionalLights = 32;

struct alignas(16) GpuLightConstants {
    GpuFloat4 PositionRange{};
    GpuFloat4 ColorIntensity{};
    GpuFloat4 DirectionOuterCone{};
    GpuFloat4 Parameters{}; // Cosine of inner cone, LightType enum, unused.
};

struct alignas(16) GpuFrameConstants {
    Mat4 ViewProjection{};
    Mat4 View{};
    Mat4 Projection{};
    Mat4 InverseProjection{};
    Mat4 LightViewProjection{};
    GpuFloat4 CameraPositionExposure{}; // World camera XYZ, exposure.
    GpuFloat4 LightDirectionIntensity{}; // World light-ray XYZ, intensity.
    GpuFloat4 LightColor{}; // Linear RGB; W unused.
    GpuFloat4 ViewportSizeInverse{}; // Width, height, 1 / width, 1 / height.
    GpuFloat4 ShadowParameters{}; // 1 / width, 1 / height, depth bias, normal bias.
    GpuFloat4 EnvironmentParameters{}; // Intensity, Y rotation, max mip, IBL enabled.
    GpuUint4 FeatureParameters{}; // Shadows enabled, SSAO enabled, PCF samples [1,16], ToneMapper enum.
    GpuUint4 AdditionalLightCount{};
    std::array<GpuLightConstants, GpuMaxAdditionalLights> AdditionalLights{};
};

struct alignas(16) GpuObjectConstants {
    Mat4 Model{};
    Mat4 NormalMatrix{}; // Inverse-transpose model matrix; translation ignored.
};

enum class GpuMaterialTexture : std::uint32_t {
    BaseColor = 1u << 0u,
    MetallicRoughness = 1u << 1u,
    Normal = 1u << 2u,
    Occlusion = 1u << 3u,
    Emissive = 1u << 4u
};

enum class GpuAlphaMode : std::uint32_t { Opaque = 0, Mask = 1, Blend = 2 };

struct alignas(16) GpuMaterialConstants {
    GpuFloat4 BaseColorFactor{1.0f, 1.0f, 1.0f, 1.0f};
    GpuFloat4 MaterialFactors{0.0f, 1.0f, 1.0f, 1.0f}; // Metallic, roughness, normal scale, AO strength.
    GpuFloat4 EmissiveAlphaCutoff{0.0f, 0.0f, 0.0f, 0.5f};
    GpuUint4 TextureMaskAlphaMode{}; // Texture bit mask, GpuAlphaMode, double-sided, receive shadows.
};

inline constexpr std::uint32_t GpuSsaoMaxSamples = 64;

struct alignas(16) GpuSsaoConstants {
    GpuFloat4 RadiusBiasPower{0.5f, 0.025f, 1.0f, 0.0f};
    GpuUint4 SampleCountFrame{32, 0, 0, 0}; // Count [1,64], frame seed, unused, unused.
    std::array<GpuFloat4, GpuSsaoMaxSamples> Kernel{}; // Tangent-space positive-Z hemisphere.
};

// Every Vulkan resource lives in set 0. Explicit vk::binding decorations and
// separate register spaces avoid register-class collisions on Vulkan.
namespace ShaderBinding {
inline constexpr std::uint32_t Frame = 0;
inline constexpr std::uint32_t ObjectOrSsao = 1;
inline constexpr std::uint32_t Material = 2;
inline constexpr std::uint32_t TextureBase = 16;
inline constexpr std::uint32_t SamplerBase = 32;
inline constexpr std::uint32_t EnvironmentSampler = SamplerBase + 3;
inline constexpr std::uint32_t UavBase = 48;
} // namespace ShaderBinding

static_assert(std::is_standard_layout_v<GpuFrameConstants>);
static_assert(std::is_trivially_copyable_v<GpuFrameConstants>);
static_assert(sizeof(GpuFloat4) == 16 && sizeof(GpuUint4) == 16);
static_assert(sizeof(Mat4) == 64);
static_assert(offsetof(GpuFrameConstants, CameraPositionExposure) == 320);
static_assert(offsetof(GpuFrameConstants, FeatureParameters) == 416);
static_assert(offsetof(GpuFrameConstants, AdditionalLights) == 448);
static_assert(sizeof(GpuLightConstants) == 64);
static_assert(sizeof(GpuFrameConstants) == 2496);
static_assert(sizeof(GpuObjectConstants) == 128);
static_assert(sizeof(GpuMaterialConstants) == 64);
static_assert(offsetof(GpuSsaoConstants, Kernel) == 32);
static_assert(sizeof(GpuSsaoConstants) == 1056);

} // namespace TinyEngine

#pragma once

#include "TinyEngine/Math.h"

#include <cstdint>
#include <array>
#include <functional>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace TinyEngine {

enum class RenderBackend { Null, VulkanNvrhi };
enum class RenderPassKind { ShadowMap, DepthPrepass, Ssao, PbrLighting, IblLighting, Compute, ToneMap, Present };
enum class ToneMapper { Aces, Reinhard, Linear };

struct RendererCapabilities {
    bool Compute = false;
    bool Hdr = false;
    bool ImageBasedLighting = false;
    std::uint32_t MaxTextureSize = 0;
    std::array<std::uint32_t, 3> MaxComputeWorkGroups{0, 0, 0};
};

struct PbrMaterial {
    enum class AlphaMode { Opaque, Mask, Blend };
    Vec3 BaseColor{1.0f, 1.0f, 1.0f};
    float Metallic = 0.0f;
    float Roughness = 1.0f;
    float NormalScale = 1.0f;
    float OcclusionStrength = 1.0f;
    Vec3 Emissive{};
    float AlphaCutoff = 0.5f;
    float BaseAlpha = 1.0f;
    AlphaMode Alpha = AlphaMode::Opaque;
    bool DoubleSided = false;
    std::int32_t BaseColorTexture = -1;
    std::int32_t MetallicRoughnessTexture = -1;
    std::int32_t NormalTexture = -1;
    std::int32_t OcclusionTexture = -1;
    std::int32_t EmissiveTexture = -1;
};

struct RenderSettings {
    bool EnableShadows = true;
    bool EnableSsao = true;
    bool EnableIbl = true;
    bool EnableCompute = false;
    std::uint32_t ShadowMapResolution = 2048;
    std::uint32_t ShadowPcfSamples = 16;
    float SsaoRadius = 0.5f;
    float SsaoBias = 0.025f;
    float Exposure = 1.0f;
    ToneMapper Tonemapper = ToneMapper::Aces;
    std::uint32_t SsaoSampleCount = 32;
    float ShadowDepthBias = 0.001f;
    bool operator==(const RenderSettings&) const = default;
};

enum class RenderResource { ShadowDepth, SceneDepth, AmbientOcclusion, HdrColor, DisplayColor, Swapchain };
enum class ResourceAccess { Read, Write };

struct ResourceUse {
    RenderResource Resource;
    ResourceAccess Access;
};

struct RenderPass {
    RenderPassKind Kind;
    std::string_view Name;
    std::vector<ResourceUse> Resources;
};

struct ComputeBuffer {
    // HLSL u0..u15, Vulkan set 0 binding 48 + Binding, RWByteAddressBuffer.
    std::uint32_t Binding = 0;
    std::vector<std::uint32_t> Words;
};

struct ComputeDispatch {
    std::string Shader;
    std::array<std::uint32_t, 3> WorkGroups{1, 1, 1};
    std::vector<ComputeBuffer> Buffers;
};

struct EnvironmentMap {
    std::string EquirectangularPath;
    float Intensity = 1.0f;
    float RotationRadians = 0.0f;
    std::uint32_t CubemapResolution = 512;
    std::uint32_t IrradianceResolution = 32;
    std::uint32_t PrefilteredMipLevels = 6;
    bool operator==(const EnvironmentMap&) const = default;
};

struct RenderFrame {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    float DeltaSeconds = 0.0f;
    std::uint64_t FrameIndex = 0;
};

class IRenderBackend {
public:
    virtual ~IRenderBackend() = default;
    [[nodiscard]] virtual RenderBackend type() const = 0;
    [[nodiscard]] virtual RendererCapabilities capabilities() const = 0;
    virtual bool beginFrame(const RenderFrame& frame, std::string* error) = 0;
    virtual bool execute(const RenderPass& pass, const RenderFrame& frame, const RenderSettings& settings, std::string* error) = 0;
    virtual void endFrame() = 0;
    virtual bool dispatch(const ComputeDispatch&, std::string* error) {
        if (error) *error = "The active renderer backend has no compute implementation";
        return false;
    }
};

class NullRenderBackend final : public IRenderBackend {
public:
    [[nodiscard]] RenderBackend type() const override { return RenderBackend::Null; }
    [[nodiscard]] RendererCapabilities capabilities() const override { return {}; }
    bool beginFrame(const RenderFrame& frame, std::string* error) override;
    bool execute(const RenderPass& pass, const RenderFrame& frame, const RenderSettings& settings, std::string* error) override;
    void endFrame() override { m_FrameOpen = false; }
    [[nodiscard]] std::uint64_t executedPassCount() const { return m_ExecutedPassCount; }

private:
    std::uint64_t m_ExecutedPassCount = 0;
    bool m_FrameOpen = false;
};

// Adapter seam for the Vulkan/NVRHI implementation. The engine never owns NVRHI
// objects here; the integration layer supplies callbacks backed by its command list
// and swapchain. This keeps the core library buildable without Vulkan headers.
struct VulkanNvrhiCallbacks {
    std::function<bool(const RenderFrame&, std::string*)> BeginFrame;
    std::function<bool(const RenderPass&, const RenderFrame&, const RenderSettings&, std::string*)> ExecutePass;
    std::function<bool(const ComputeDispatch&, std::string*)> DispatchCompute;
    std::function<void()> EndFrame;
};

class VulkanNvrhiBackend final : public IRenderBackend {
public:
    VulkanNvrhiBackend(RendererCapabilities capabilities, VulkanNvrhiCallbacks callbacks)
        : m_Capabilities(capabilities), m_Callbacks(std::move(callbacks)) {}
    [[nodiscard]] RenderBackend type() const override { return RenderBackend::VulkanNvrhi; }
    [[nodiscard]] RendererCapabilities capabilities() const override { return m_Capabilities; }
    bool beginFrame(const RenderFrame& frame, std::string* error) override;
    bool execute(const RenderPass& pass, const RenderFrame& frame, const RenderSettings& settings, std::string* error) override;
    bool dispatch(const ComputeDispatch& dispatch, std::string* error) override;
    void endFrame() override;

private:
    RendererCapabilities m_Capabilities{};
    VulkanNvrhiCallbacks m_Callbacks;
    bool m_FrameOpen = false;
};

class Renderer {
public:
    explicit Renderer(IRenderBackend& backend) : m_Backend(backend) {}
    [[nodiscard]] const RenderSettings& settings() const { return m_Settings; }
    bool setSettings(const RenderSettings& settings, std::string* error = nullptr);
    [[nodiscard]] std::vector<RenderPass> buildPasses() const;
    bool render(const RenderFrame& frame, std::string* error = nullptr);
    bool queueCompute(ComputeDispatch dispatch, std::string* error = nullptr);
    bool setEnvironment(const EnvironmentMap& environment, std::string* error = nullptr);
    [[nodiscard]] const EnvironmentMap& environment() const { return m_Environment; }
    [[nodiscard]] const std::vector<ComputeDispatch>& computeQueue() const { return m_ComputeQueue; }
    [[nodiscard]] IRenderBackend& backend() { return m_Backend; }

private:
    IRenderBackend& m_Backend;
    RenderSettings m_Settings{};
    EnvironmentMap m_Environment{};
    std::vector<ComputeDispatch> m_ComputeQueue;
};

// Expects linear HDR input and returns linear display-referred values.
[[nodiscard]] Vec3 toneMap(const Vec3& linearHdr, float exposure, ToneMapper mapper);
[[nodiscard]] bool validatePbrMaterial(const PbrMaterial& material, std::string* error = nullptr);
// Pure, thread-safe validation shared by scene loading and renderer boundaries.
// Failures leave caller-owned settings unchanged and provide a diagnostic.
[[nodiscard]] bool ValidateRenderSettings(const RenderSettings& settings, std::string* error = nullptr);
[[nodiscard]] bool ValidateEnvironmentMap(const EnvironmentMap& environment, std::string* error = nullptr);
// Pure validation; thread safe. Up to 16 unique raw buffers, 64 MiB total.
[[nodiscard]] bool ValidateComputeDispatch(const ComputeDispatch& dispatch,
    const std::array<std::uint32_t, 3>& maxWorkGroups, std::string* error = nullptr);
// Main-thread asset operations. Apply scalar overrides while preserving glTF
// texture references. Malformed/unsupported input leaves material unchanged.
[[nodiscard]] bool ApplyPbrMaterialOverride(std::string_view text, PbrMaterial& material, std::string* error = nullptr);
[[nodiscard]] bool LoadPbrMaterialOverride(const std::filesystem::path& path, PbrMaterial& material, std::string* error = nullptr);
[[nodiscard]] bool validatePassDependencies(std::span<const RenderPass> passes, std::string* error = nullptr);
[[nodiscard]] std::vector<Vec3> generateSsaoKernel(std::uint32_t sampleCount);

} // namespace TinyEngine

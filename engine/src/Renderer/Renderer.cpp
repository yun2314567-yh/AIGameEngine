#include "TinyEngine/Renderer/Renderer.h"
#include "TinyEngine/Renderer/Components.h"

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace TinyEngine {
namespace {

float aces(float value) {
    constexpr float a = 2.51f;
    constexpr float b = 0.03f;
    constexpr float c = 2.43f;
    constexpr float d = 0.59f;
    constexpr float e = 0.14f;
    return std::clamp((value * (a * value + b)) / (value * (c * value + d) + e), 0.0f, 1.0f);
}

} // namespace

bool NullRenderBackend::beginFrame(const RenderFrame& frame, std::string* error) {
    if (m_FrameOpen) {
        if (error) *error = "A render frame is already open";
        return false;
    }
    if (frame.Width == 0 || frame.Height == 0) {
        if (error) *error = "Cannot render a frame with zero dimensions";
        return false;
    }
    m_FrameOpen = true;
    return true;
}

bool NullRenderBackend::execute(const RenderPass&, const RenderFrame&, const RenderSettings&, std::string* error) {
    if (!m_FrameOpen) {
        if (error) *error = "Cannot execute a render pass outside a frame";
        return false;
    }
    ++m_ExecutedPassCount;
    return true;
}

bool VulkanNvrhiBackend::beginFrame(const RenderFrame& frame, std::string* error) {
    if (m_FrameOpen) {
        if (error) *error = "A Vulkan/NVRHI frame is already open";
        return false;
    }
    if (!m_Callbacks.BeginFrame) {
        if (error) *error = "Vulkan/NVRHI BeginFrame callback is not configured";
        return false;
    }
    if (!m_Callbacks.BeginFrame(frame, error)) return false;
    m_FrameOpen = true;
    return true;
}

bool VulkanNvrhiBackend::execute(const RenderPass& pass, const RenderFrame& frame, const RenderSettings& settings, std::string* error) {
    if (!m_FrameOpen || !m_Callbacks.ExecutePass) {
        if (error) *error = m_FrameOpen ? "Vulkan/NVRHI ExecutePass callback is not configured" : "Cannot execute a Vulkan/NVRHI pass outside a frame";
        return false;
    }
    return m_Callbacks.ExecutePass(pass, frame, settings, error);
}

bool VulkanNvrhiBackend::dispatch(const ComputeDispatch& dispatchValue, std::string* error) {
    if (!m_FrameOpen || !m_Callbacks.DispatchCompute) {
        if (error) *error = m_FrameOpen ? "Vulkan/NVRHI DispatchCompute callback is not configured" : "Cannot dispatch Vulkan/NVRHI compute outside a frame";
        return false;
    }
    return m_Callbacks.DispatchCompute(dispatchValue, error);
}

void VulkanNvrhiBackend::endFrame() {
    if (m_FrameOpen && m_Callbacks.EndFrame) m_Callbacks.EndFrame();
    m_FrameOpen = false;
}

bool ValidateRenderSettings(const RenderSettings& settings, std::string* error) {
    if (error) error->clear();
    if (settings.ShadowMapResolution == 0 || settings.ShadowMapResolution > 16384 ||
        settings.ShadowPcfSamples == 0 || settings.ShadowPcfSamples > 16 ||
        settings.SsaoSampleCount == 0 || settings.SsaoSampleCount > 64 ||
        static_cast<unsigned>(settings.Tonemapper) > static_cast<unsigned>(ToneMapper::Linear)) {
        if (error) *error = "Shadow resolution must be in [1,16384], PCF samples in [1,16], SSAO samples in [1,64], and tone mapper valid";
        return false;
    }
    if (!std::isfinite(settings.SsaoRadius) || settings.SsaoRadius <= 0.0f ||
        !std::isfinite(settings.SsaoBias) || settings.SsaoBias < 0.0f ||
        !std::isfinite(settings.Exposure) || settings.Exposure < 0.0f ||
        !std::isfinite(settings.ShadowDepthBias) || settings.ShadowDepthBias < 0.0f) {
        if (error) *error = "SSAO radius must be positive; bias and exposure must be finite and non-negative";
        return false;
    }
    return true;
}

bool Renderer::setSettings(const RenderSettings& settings, std::string* error) {
    if (!ValidateRenderSettings(settings, error)) return false;
    if (settings.EnableCompute && !m_Backend.capabilities().Compute) {
        if (error) *error = "The active renderer backend does not support compute";
        return false;
    }
    if (m_Backend.capabilities().MaxTextureSize != 0 && settings.ShadowMapResolution > m_Backend.capabilities().MaxTextureSize) {
        if (error) *error = "Shadow map resolution exceeds the backend texture limit";
        return false;
    }
    m_Settings = settings;
    return true;
}

std::vector<RenderPass> Renderer::buildPasses() const {
    std::vector<RenderPass> passes;
    if (m_Settings.EnableShadows) passes.push_back({RenderPassKind::ShadowMap, "Soft Shadow Maps", {{RenderResource::ShadowDepth, ResourceAccess::Write}}});
    passes.push_back({RenderPassKind::DepthPrepass, "Depth Prepass", {{RenderResource::SceneDepth, ResourceAccess::Write}}});
    if (m_Settings.EnableSsao) passes.push_back({RenderPassKind::Ssao, "Screen Space Ambient Occlusion", {{RenderResource::SceneDepth, ResourceAccess::Read}, {RenderResource::AmbientOcclusion, ResourceAccess::Write}}});
    RenderPass lighting{RenderPassKind::PbrLighting, "PBR Lighting", {{RenderResource::SceneDepth, ResourceAccess::Read}, {RenderResource::HdrColor, ResourceAccess::Write}}};
    if (m_Settings.EnableShadows) lighting.Resources.push_back({RenderResource::ShadowDepth, ResourceAccess::Read});
    if (m_Settings.EnableSsao) lighting.Resources.push_back({RenderResource::AmbientOcclusion, ResourceAccess::Read});
    passes.push_back(std::move(lighting));
    if (m_Settings.EnableIbl && m_Backend.capabilities().ImageBasedLighting) {
        passes.push_back({RenderPassKind::IblLighting, "Image Based Lighting", {{RenderResource::HdrColor, ResourceAccess::Read}, {RenderResource::HdrColor, ResourceAccess::Write}}});
    }
    if (m_Settings.EnableCompute && m_Backend.capabilities().Compute && !m_ComputeQueue.empty()) passes.push_back({RenderPassKind::Compute, "Compute", {{RenderResource::HdrColor, ResourceAccess::Read}, {RenderResource::HdrColor, ResourceAccess::Write}}});
    passes.push_back({RenderPassKind::ToneMap, "HDR Tone Mapping", {{RenderResource::HdrColor, ResourceAccess::Read}, {RenderResource::DisplayColor, ResourceAccess::Write}}});
    passes.push_back({RenderPassKind::Present, "Present", {{RenderResource::DisplayColor, ResourceAccess::Read}, {RenderResource::Swapchain, ResourceAccess::Write}}});
    return passes;
}

bool Renderer::render(const RenderFrame& frame, std::string* error) {
    if (!std::isfinite(frame.DeltaSeconds) || frame.DeltaSeconds < 0.0f) {
        if (error) *error = "Frame time must be finite and non-negative";
        return false;
    }
    const auto passes = buildPasses();
    if (!validatePassDependencies(passes, error)) return false;
    if (!m_Backend.beginFrame(frame, error)) return false;
    for (const RenderPass& pass : passes) {
        if (!m_Backend.execute(pass, frame, m_Settings, error)) {
            m_Backend.endFrame();
            return false;
        }
        if (pass.Kind == RenderPassKind::Compute) {
            for (const auto& dispatch : m_ComputeQueue) if (!m_Backend.dispatch(dispatch, error)) { m_Backend.endFrame(); return false; }
        }
    }
    m_Backend.endFrame();
    m_ComputeQueue.clear();
    return true;
}

Vec3 toneMap(const Vec3& linearHdr, float exposure, ToneMapper mapper) {
    const auto map = [exposure, mapper](float channel) {
        const float value = std::isfinite(channel) && std::isfinite(exposure) ? std::max(channel * exposure, 0.0f) : 0.0f;
        switch (mapper) {
        case ToneMapper::Aces: return aces(value);
        case ToneMapper::Reinhard: return value / (1.0f + value);
        case ToneMapper::Linear: return std::clamp(value, 0.0f, 1.0f);
        }
        return 0.0f;
    };
    return {map(linearHdr.x), map(linearHdr.y), map(linearHdr.z)};
}

bool Renderer::queueCompute(ComputeDispatch dispatch, std::string* error) {
    const auto capabilities = m_Backend.capabilities();
    if (!m_Settings.EnableCompute || !capabilities.Compute || dispatch.Shader.empty()) {
        if (error) *error = "Compute dispatch requires an enabled compute backend and a shader";
        return false;
    }
    if (!ValidateComputeDispatch(dispatch, capabilities.MaxComputeWorkGroups, error)) return false;
    m_ComputeQueue.push_back(std::move(dispatch));
    return true;
}

bool ValidateComputeDispatch(const ComputeDispatch& dispatch,
    const std::array<std::uint32_t, 3>& maxWorkGroups, std::string* error) {
    const auto fail = [error](const char* message) { if (error) *error = message; return false; };
    const std::filesystem::path path{dispatch.Shader};
    if (path.empty() || path.has_root_path() || dispatch.Shader.find(':') != std::string::npos)
        return fail("Compute shader must be relative to the shader directory");
    for (const auto& part : path) if (part == "..") return fail("Compute shader path escapes the shader directory");
    for (std::size_t axis = 0; axis < 3; ++axis)
        if (dispatch.WorkGroups[axis] == 0 || dispatch.WorkGroups[axis] > maxWorkGroups[axis])
            return fail("Compute work group count exceeds device limits");
    if (dispatch.Buffers.size() > 16) return fail("Compute supports up to 16 raw buffers");
    std::array<bool, 16> used{};
    std::size_t totalWords = 0;
    constexpr std::size_t maxWords = 64 * 1024 * 1024 / sizeof(std::uint32_t);
    for (const auto& buffer : dispatch.Buffers) {
        if (buffer.Binding >= used.size() || used[buffer.Binding]) return fail("Compute buffer bindings must be unique and in [0,15]");
        used[buffer.Binding] = true;
        if (buffer.Words.empty() || buffer.Words.size() > maxWords - totalWords)
            return fail("Compute buffers must be nonempty and fit in 64 MiB total");
        totalWords += buffer.Words.size();
    }
    return true;
}

bool ValidateEnvironmentMap(const EnvironmentMap& environment, std::string* error) {
    if (error) error->clear();
    const std::filesystem::path path{environment.EquirectangularPath};
    if (path.has_root_path() || environment.EquirectangularPath.find(':') != std::string::npos ||
        environment.EquirectangularPath.find('\0') != std::string::npos) {
        if (error) *error = "Environment path must be relative to the asset root";
        return false;
    }
    for (const auto& part : path) if (part == "..") {
        if (error) *error = "Environment path escapes the asset root";
        return false;
    }
    if (!std::isfinite(environment.Intensity) || environment.Intensity < 0.0f || !std::isfinite(environment.RotationRadians) || environment.CubemapResolution == 0 || environment.CubemapResolution > 512 || environment.IrradianceResolution == 0 || environment.IrradianceResolution > 128 || environment.PrefilteredMipLevels == 0) {
        if (error) *error = "Environment settings contain invalid dimensions or values";
        return false;
    }
    if (environment.PrefilteredMipLevels > 1u + static_cast<std::uint32_t>(std::floor(std::log2(static_cast<double>(environment.CubemapResolution))))) {
        if (error) *error = "Environment mip levels exceed the cubemap resolution";
        return false;
    }
    return true;
}

bool Renderer::setEnvironment(const EnvironmentMap& environment, std::string* error) {
    if (!ValidateEnvironmentMap(environment, error)) return false;
    const auto capabilities = m_Backend.capabilities();
    if (!capabilities.Hdr || !capabilities.ImageBasedLighting) {
        if (error) *error = "The active renderer backend does not support HDR image-based lighting";
        return false;
    }
    m_Environment = environment;
    return true;
}

bool ValidateLight(const LightComponent& light, std::string* error) {
    if (error) error->clear();
    const auto nonnegative = [](float value) { return std::isfinite(value) && value >= 0; };
    if (static_cast<unsigned>(light.Type) > static_cast<unsigned>(LightType::Spot) ||
        !nonnegative(light.Color.x) || !nonnegative(light.Color.y) || !nonnegative(light.Color.z) ||
        !nonnegative(light.Intensity) || !std::isfinite(light.Range) || light.Range <= 0 ||
        !nonnegative(light.InnerConeRadians) || !std::isfinite(light.OuterConeRadians) ||
        light.OuterConeRadians < light.InnerConeRadians || light.OuterConeRadians > 1.570796327f) {
        if (error) *error = "Light values must be finite and nonnegative, with positive range and ordered cones in [0, pi/2]";
        return false;
    }
    return true;
}

bool validatePbrMaterial(const PbrMaterial& material, std::string* error) {
    const auto unit = [](float value) { return std::isfinite(value) && value >= 0.0f && value <= 1.0f; };
    const auto nonnegative = [](const Vec3& value) { return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z) && value.x >= 0.0f && value.y >= 0.0f && value.z >= 0.0f; };
    if (!nonnegative(material.BaseColor) || !nonnegative(material.Emissive) || !unit(material.Metallic) || !unit(material.Roughness) || !unit(material.OcclusionStrength) || !unit(material.AlphaCutoff) || !unit(material.BaseAlpha) ||
        static_cast<unsigned>(material.Alpha) > static_cast<unsigned>(PbrMaterial::AlphaMode::Blend) ||
        !std::isfinite(material.NormalScale) || material.NormalScale < 0.0f) {
        if (error) *error = "PBR material contains non-finite values or factors outside their supported range";
        return false;
    }
    return true;
}

bool validatePassDependencies(std::span<const RenderPass> passes, std::string* error) {
    std::unordered_set<RenderResource> initialized;
    for (const auto& pass : passes) for (const auto& resource : pass.Resources) {
        if (resource.Access == ResourceAccess::Read && !initialized.contains(resource.Resource)) {
            if (error) *error = "Render pass reads a resource before it is written: " + std::string(pass.Name);
            return false;
        }
        if (resource.Access == ResourceAccess::Write) initialized.insert(resource.Resource);
    }
    return true;
}

std::vector<Vec3> generateSsaoKernel(std::uint32_t sampleCount) {
    std::vector<Vec3> samples;
    if (sampleCount == 0 || sampleCount > 128) return samples;
    samples.reserve(sampleCount);
    constexpr float goldenAngle = 2.39996322972865332f;
    for (std::uint32_t index = 0; index < sampleCount; ++index) {
        const float t = (static_cast<float>(index) + 0.5f) / static_cast<float>(sampleCount);
        const float z = t;
        const float radius = std::sqrt(1.0f - z * z);
        const float angle = goldenAngle * static_cast<float>(index);
        const float scale = 0.1f + 0.9f * t * t;
        samples.push_back(Vec3{radius * std::cos(angle), radius * std::sin(angle), z} * scale);
    }
    return samples;
}

} // namespace TinyEngine

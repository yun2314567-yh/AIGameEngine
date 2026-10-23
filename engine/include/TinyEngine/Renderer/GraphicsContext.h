#pragma once

#include "TinyEngine/Math.h"
#include "TinyEngine/Renderer/Renderer.h"

#include <filesystem>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace TinyEngine {

class Scene;
class Window;

struct FrameCapture {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    // Row-major, linear display-referred RGBA values after tone mapping.
    std::vector<float> Pixels;
};

// Owns a Vulkan surface, device, NVRHI resources and swapchain. All methods,
// including destruction, must run on the window's owning thread.
class GraphicsContext {
public:
    ~GraphicsContext();
    GraphicsContext(const GraphicsContext&) = delete;
    GraphicsContext& operator=(const GraphicsContext&) = delete;

    // Returns null with an error when the build or device lacks Vulkan support.
    static std::unique_ptr<GraphicsContext> create(Window& window,
        const std::filesystem::path& shaderDirectory, std::string* error = nullptr);
    static bool available();
    bool initializeImGui(std::string* error = nullptr);
    void setAssetRoot(std::filesystem::path assetRoot);
    // Synchronously filters/uploads an HDRI. Empty path restores the built-in
    // environment. Failure preserves the previous environment and returns false.
    bool setEnvironment(const EnvironmentMap& environment, std::string* error = nullptr);
    bool render(Scene& scene, const RenderSettings& settings, const Mat4& view,
        const Mat4& projection, const Vec3& cameraPosition, void* imguiDrawData = nullptr,
        std::string* error = nullptr);
    // Synchronous SPIR-V dispatch. Buffers are uploaded as raw UAVs; the shader
    // must use the declared binding/size contract. No other descriptors allowed.
    // Failure returns false with error. Call on the window's owning thread.
    bool dispatch(const ComputeDispatch& dispatch, std::string* error = nullptr);
    // Reads every raw buffer back after execution, preserving binding order.
    // On failure destination is unchanged. Same threading rules as render().
    bool dispatch(const ComputeDispatch& dispatch, std::vector<ComputeBuffer>& destination,
        std::string* error = nullptr);
    // Synchronously reads the last completed frame. On failure destination is
    // unchanged. Must be called on the window's owning thread after render().
    bool CaptureFrame(FrameCapture& destination, std::string* error = nullptr);

private:
    GraphicsContext();
    struct Impl;
    std::unique_ptr<Impl> m_Impl;
};

// Right-handed camera helpers with Vulkan's [0,1] depth range and clip Y flip.
Mat4 perspectiveVulkan(float verticalFovRadians, float aspect, float nearClip, float farClip);
Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& up = {0.0f, 1.0f, 0.0f});
Mat4 orthographicVulkan(float left, float right, float bottom, float top, float nearClip, float farClip);
bool inverse(const Mat4& matrix, Mat4& destination);

} // namespace TinyEngine

#include "TinyEngine/Renderer/GraphicsContext.h"

#include "TinyEngine/Scene.h"
#include "TinyEngine/Platform/Window.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace TinyEngine {

#if !defined(TINYENGINE_WITH_VULKAN_NVRHI) || !defined(TINYENGINE_WITH_GLFW)
struct GraphicsContext::Impl {
    std::filesystem::path ShaderDirectory;
    std::filesystem::path AssetRoot;
};

GraphicsContext::GraphicsContext() : m_Impl(std::make_unique<Impl>()) {}
GraphicsContext::~GraphicsContext() = default;

bool GraphicsContext::available() {
#ifdef TINYENGINE_WITH_VULKAN_NVRHI
    return true;
#else
    return false;
#endif
}

std::unique_ptr<GraphicsContext> GraphicsContext::create(Window& window,
    const std::filesystem::path& shaderDirectory, std::string* error) {
    (void)window;
    (void)shaderDirectory;
#ifdef TINYENGINE_WITH_VULKAN_NVRHI
    if (error) *error = "Vulkan/NVRHI graphics context initialization is unavailable on this platform";
#else
    if (error) *error = "TinyEngine was built without Vulkan/NVRHI support";
#endif
    return {};
}

bool GraphicsContext::initializeImGui(std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}

void GraphicsContext::setAssetRoot(std::filesystem::path assetRoot) {
    m_Impl->AssetRoot = std::move(assetRoot);
}

bool GraphicsContext::setEnvironment(const EnvironmentMap&, std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}

bool GraphicsContext::render(Scene&, const RenderSettings&, const Mat4&, const Mat4&,
    const Vec3&, void*, std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}

bool GraphicsContext::dispatch(const ComputeDispatch&, std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}

bool GraphicsContext::dispatch(const ComputeDispatch&, std::vector<ComputeBuffer>&, std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}

bool GraphicsContext::CaptureFrame(FrameCapture&, std::string* error) {
    if (error) *error = "Graphics context is not initialized";
    return false;
}
#endif

Mat4 perspectiveVulkan(float verticalFovRadians, float aspect, float nearClip, float farClip) {
    Mat4 result{};
    if (!std::isfinite(verticalFovRadians) || !std::isfinite(aspect) ||
        !std::isfinite(nearClip) || !std::isfinite(farClip) ||
        verticalFovRadians <= 0.0f || verticalFovRadians >= 3.14159265358979323846f ||
        aspect <= 0.0f || nearClip <= 0.0f || farClip <= nearClip) {
        return result;
    }
    const float focal = 1.0f / std::tan(verticalFovRadians * 0.5f);
    result(0, 0) = focal / aspect;
    result(1, 1) = -focal;
    result(2, 2) = farClip / (nearClip - farClip);
    result(2, 3) = -1.0f;
    result(3, 2) = nearClip * farClip / (nearClip - farClip);
    return result;
}

Mat4 lookAt(const Vec3& eye, const Vec3& target, const Vec3& upValue) {
    const Vec3 forward = (target - eye).normalized();
    Vec3 right = cross(forward, upValue).normalized();
    if (forward.lengthSquared() == 0.0f) return Mat4::identity();
    if (right.lengthSquared() == 0.0f) {
        const Vec3 fallback = std::fabs(forward.y) < 0.99f ? Vec3{0.0f, 1.0f, 0.0f} : Vec3{1.0f, 0.0f, 0.0f};
        right = cross(forward, fallback).normalized();
    }
    const Vec3 up = cross(right, forward).normalized();
    Mat4 result = Mat4::identity();
    result(0, 0) = right.x; result(1, 0) = right.y; result(2, 0) = right.z;
    result(0, 1) = up.x; result(1, 1) = up.y; result(2, 1) = up.z;
    result(0, 2) = -forward.x; result(1, 2) = -forward.y; result(2, 2) = -forward.z;
    result(3, 0) = -dot(right, eye);
    result(3, 1) = -dot(up, eye);
    result(3, 2) = dot(forward, eye);
    return result;
}

Mat4 orthographicVulkan(float left, float right, float bottom, float top, float nearClip, float farClip) {
    Mat4 result{};
    if (!std::isfinite(left) || !std::isfinite(right) || !std::isfinite(bottom) ||
        !std::isfinite(top) || !std::isfinite(nearClip) || !std::isfinite(farClip) ||
        right == left || top == bottom || farClip <= nearClip) return result;
    result(0, 0) = 2.0f / (right - left);
    result(1, 1) = -2.0f / (top - bottom);
    result(2, 2) = 1.0f / (nearClip - farClip);
    result(3, 0) = -(right + left) / (right - left);
    result(3, 1) = (top + bottom) / (bottom - top);
    result(3, 2) = nearClip / (nearClip - farClip);
    result(3, 3) = 1.0f;
    return result;
}

bool inverse(const Mat4& matrix, Mat4& destination) {
    std::array<std::array<double, 8>, 4> rows{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const float value = matrix(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row));
            if (!std::isfinite(value)) return false;
            rows[row][column] = static_cast<double>(value);
        }
        rows[row][row + 4] = 1.0;
    }
    for (std::size_t column = 0; column < 4; ++column) {
        std::size_t pivot = column;
        for (std::size_t row = column + 1; row < 4; ++row) {
            if (std::fabs(rows[row][column]) > std::fabs(rows[pivot][column])) pivot = row;
        }
        if (std::fabs(rows[pivot][column]) <= std::numeric_limits<double>::epsilon()) return false;
        std::swap(rows[pivot], rows[column]);
        const double divisor = rows[column][column];
        for (double& value : rows[column]) value /= divisor;
        for (std::size_t row = 0; row < 4; ++row) {
            if (row == column) continue;
            const double factor = rows[row][column];
            for (std::size_t entry = 0; entry < 8; ++entry) rows[row][entry] -= factor * rows[column][entry];
        }
    }
    Mat4 result{};
    for (std::size_t row = 0; row < 4; ++row) {
        for (std::size_t column = 0; column < 4; ++column) {
            const double value = rows[row][column + 4];
            if (!std::isfinite(value) || std::fabs(value) > std::numeric_limits<float>::max()) return false;
            result(static_cast<std::uint32_t>(column), static_cast<std::uint32_t>(row)) = static_cast<float>(value);
        }
    }
    destination = result;
    return true;
}

} // namespace TinyEngine

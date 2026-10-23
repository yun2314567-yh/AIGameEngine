#include "TinyEngine/Renderer/Environment.h"
#include "TinyEngine/Math.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <exception>

namespace TinyEngine {
namespace {
constexpr float pi = std::numbers::pi_v<float>;
constexpr std::uint32_t sampleCount = 128;

float radicalInverse(std::uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xaaaaaaaau) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xccccccccu) >> 2u);
    bits = ((bits & 0x0f0f0f0fu) << 4u) | ((bits & 0xf0f0f0f0u) >> 4u);
    bits = ((bits & 0x00ff00ffu) << 8u) | ((bits & 0xff00ff00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f;
}

Vec3 directionAt(std::uint32_t x, std::uint32_t y, std::uint32_t width, std::uint32_t height) {
    const float theta = (static_cast<float>(y) + 0.5f) * pi / static_cast<float>(height);
    const float phi = ((static_cast<float>(x) + 0.5f) / static_cast<float>(width) - 0.5f) * 2.0f * pi;
    return {std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi)};
}

Vec3 sample(std::span<const float> pixels, std::uint32_t width, std::uint32_t height, const Vec3& direction) {
    const float u = std::atan2(direction.z, direction.x) / (2.0f * pi) + 0.5f;
    const float v = std::acos(std::clamp(direction.y, -1.0f, 1.0f)) / pi;
    const float px = u * static_cast<float>(width) - 0.5f;
    const float py = std::clamp(v * static_cast<float>(height) - 0.5f, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(px));
    const auto y0 = static_cast<std::uint32_t>(py);
    const float tx = px - static_cast<float>(x0), ty = py - static_cast<float>(y0);
    const auto read = [&](int x, std::uint32_t y) {
        const int wrapped = (x % static_cast<int>(width) + static_cast<int>(width)) % static_cast<int>(width);
        const auto offset = (static_cast<std::size_t>(y) * width + static_cast<std::uint32_t>(wrapped)) * 4;
        return Vec3{pixels[offset], pixels[offset + 1], pixels[offset + 2]};
    };
    const auto y1 = std::min(y0 + 1, height - 1);
    return (read(x0, y0) * (1 - tx) + read(x0 + 1, y0) * tx) * (1 - ty)
         + (read(x0, y1) * (1 - tx) + read(x0 + 1, y1) * tx) * ty;
}

void store(EnvironmentImage& image, std::uint32_t x, std::uint32_t y, const Vec3& value) {
    const auto offset = (static_cast<std::size_t>(y) * image.Width + x) * 4;
    image.Pixels[offset] = value.x; image.Pixels[offset + 1] = value.y;
    image.Pixels[offset + 2] = value.z; image.Pixels[offset + 3] = 1;
}
} // namespace

bool FilterEnvironment(std::span<const float> rgba, std::uint32_t width, std::uint32_t height,
    std::uint32_t resolution, std::uint32_t mipLevels, std::uint32_t irradianceResolution,
    FilteredEnvironment& destination, std::string* error) try {
    const auto fail = [error](const char* message) { if (error) *error = message; return false; };
    if (width == 0 || height == 0 || width > 16384 || height > 8192 ||
        static_cast<std::uint64_t>(width) * height > 16 * 1024 * 1024 ||
        rgba.size() != static_cast<std::size_t>(width) * height * 4)
        return fail("Invalid or oversized environment pixels");
    if (resolution == 0 || resolution > 512 || irradianceResolution == 0 || irradianceResolution > 128 ||
        mipLevels == 0 || mipLevels > 1u + static_cast<std::uint32_t>(std::log2(resolution)))
        return fail("Invalid environment filter dimensions");
    for (const float value : rgba) if (!std::isfinite(value) || value < 0 || value > 1.0e10f)
        return fail("Environment pixels must be finite and in [0, 1e10]");
    FilteredEnvironment result;
    const auto filter = [&](std::uint32_t outputWidth, std::uint32_t outputHeight, float roughness, bool diffuse) {
        EnvironmentImage image{outputWidth, outputHeight, std::vector<float>(static_cast<std::size_t>(outputWidth) * outputHeight * 4)};
        for (std::uint32_t y = 0; y < outputHeight; ++y) for (std::uint32_t x = 0; x < outputWidth; ++x) {
            const auto normal = directionAt(x, y, outputWidth, outputHeight);
            if (!diffuse && roughness == 0) { store(image, x, y, sample(rgba, width, height, normal)); continue; }
            const Vec3 axis = std::fabs(normal.z) < 0.99f ? Vec3{0, 0, 1} : Vec3{0, 1, 0};
            const Vec3 tangent = cross(axis, normal).normalized(), bitangent = cross(normal, tangent);
            Vec3 sum{};
            float weight = 0;
            const float alpha = roughness * roughness;
            for (std::uint32_t index = 0; index < sampleCount; ++index) {
                const float xi = (static_cast<float>(index) + 0.5f) / sampleCount;
                const float phi = 2 * pi * radicalInverse(index);
                const float cosTheta = diffuse ? std::sqrt(1 - xi) : std::sqrt((1 - xi) / (1 + (alpha * alpha - 1) * xi));
                const float sinTheta = std::sqrt(std::max(0.0f, 1 - cosTheta * cosTheta));
                const auto local = tangent * (std::cos(phi) * sinTheta) + bitangent * (std::sin(phi) * sinTheta) + normal * cosTheta;
                const Vec3 light = diffuse ? local : local * (2 * dot(normal, local)) - normal;
                const float sampleWeight = diffuse ? 1.0f : std::max(dot(normal, light), 0.0f);
                if (sampleWeight > 0) { sum += sample(rgba, width, height, light) * sampleWeight; weight += sampleWeight; }
            }
            store(image, x, y, sum / std::max(weight, 0.00001f));
        }
        return image;
    };
    for (std::uint32_t level = 0; level < mipLevels; ++level) {
        const auto mipHeight = std::max(1u, resolution >> level);
        const float roughness = mipLevels > 1 ? static_cast<float>(level) / static_cast<float>(mipLevels - 1) : 0;
        result.SpecularMips.push_back(filter(std::max(1u, (resolution * 2) >> level), mipHeight, roughness, false));
    }
    result.Irradiance = filter(irradianceResolution * 2, irradianceResolution, 1, true);
    constexpr std::uint32_t brdfSize = 64;
    result.Brdf = {brdfSize, brdfSize, std::vector<float>(brdfSize * brdfSize * 4)};
    for (std::uint32_t y = 0; y < brdfSize; ++y) for (std::uint32_t x = 0; x < brdfSize; ++x) {
        const float nDotV = (static_cast<float>(x) + 0.5f) / brdfSize;
        const float roughness = (static_cast<float>(y) + 0.5f) / brdfSize;
        const float alpha = roughness * roughness, k = alpha * 0.5f;
        const Vec3 view{std::sqrt(1 - nDotV * nDotV), 0, nDotV};
        float a = 0, b = 0;
        for (std::uint32_t index = 0; index < sampleCount; ++index) {
            const float xi = (static_cast<float>(index) + 0.5f) / sampleCount;
            const float phi = 2 * pi * radicalInverse(index);
            const float nDotH = std::sqrt((1 - xi) / (1 + (alpha * alpha - 1) * xi));
            const float sinTheta = std::sqrt(std::max(0.0f, 1 - nDotH * nDotH));
            const Vec3 halfVector{std::cos(phi) * sinTheta, std::sin(phi) * sinTheta, nDotH};
            const float vDotH = std::max(dot(view, halfVector), 0.0f);
            const float nDotL = std::max(2 * vDotH * nDotH - nDotV, 0.0f);
            if (nDotL <= 0) continue;
            const float geometry = (nDotV / (nDotV * (1 - k) + k)) * (nDotL / (nDotL * (1 - k) + k));
            const float visibility = geometry * vDotH / std::max(nDotH * nDotV, 0.00001f);
            const float fresnel = std::pow(1 - vDotH, 5.0f);
            a += (1 - fresnel) * visibility; b += fresnel * visibility;
        }
        store(result.Brdf, x, y, {a / sampleCount, b / sampleCount, 0});
    }
    destination = std::move(result);
    return true;
} catch (const std::exception& exception) {
    if (error) *error = std::string("Environment filtering failed: ") + exception.what();
    return false;
}
} // namespace TinyEngine

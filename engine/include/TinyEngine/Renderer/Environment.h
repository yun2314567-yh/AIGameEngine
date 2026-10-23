#pragma once

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace TinyEngine {

struct EnvironmentImage {
    std::uint32_t Width = 0;
    std::uint32_t Height = 0;
    std::vector<float> Pixels; // Linear nonnegative RGBA, equirectangular Y-up.
};

struct FilteredEnvironment {
    std::vector<EnvironmentImage> SpecularMips;
    EnvironmentImage Irradiance; // Cosine integral divided by pi.
    EnvironmentImage Brdf; // Split-sum GGX coefficients in R/G; NdotV on X.
};

// Pure CPU preprocessing, thread safe. Uses deterministic Hammersley samples
// for GGX specular convolution and cosine-weighted diffuse convolution. Input
// is limited to 16M pixels; output to 1024x512. Failure preserves destination.
bool FilterEnvironment(std::span<const float> rgba, std::uint32_t width, std::uint32_t height,
    std::uint32_t resolution, std::uint32_t mipLevels, std::uint32_t irradianceResolution,
    FilteredEnvironment& destination, std::string* error = nullptr);

} // namespace TinyEngine

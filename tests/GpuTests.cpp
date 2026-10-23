#include "TinyEngine/Platform/Window.h"
#include "TinyEngine/Renderer/GraphicsContext.h"
#include "TinyEngine/Scene.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

namespace {
void require(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

double difference(const TinyEngine::FrameCapture& a, const TinyEngine::FrameCapture& b) {
    require(a.Width == b.Width && a.Height == b.Height, "Capture dimensions differ");
    double total = 0;
    for (std::size_t index = 0; index < a.Pixels.size(); ++index) {
        require(std::isfinite(a.Pixels[index]) && std::isfinite(b.Pixels[index]), "Non-finite frame pixel");
        total += std::abs(a.Pixels[index] - b.Pixels[index]);
    }
    return total / static_cast<double>(a.Pixels.size());
}

void savePpm(const TinyEngine::FrameCapture& frame, const std::filesystem::path& path) {
    std::ofstream file(path, std::ios::binary);
    file << "P6\n" << frame.Width << ' ' << frame.Height << "\n255\n";
    for (std::size_t index = 0; index < frame.Pixels.size(); index += 4) {
        for (std::size_t channel = 0; channel < 3; ++channel) {
            const float linear = std::clamp(frame.Pixels[index + channel], 0.0f, 1.0f);
            const float srgb = linear <= 0.0031308f ? linear * 12.92f : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
            file.put(static_cast<char>(static_cast<unsigned char>(std::lround(srgb * 255.0f))));
        }
    }
    require(static_cast<bool>(file), "Could not save GPU capture");
}
}

int main() {
    using namespace TinyEngine;
    try {
        std::string error;
        std::cerr << "GPU: creating window\n";
        auto window = Window::create({"TinyEngine GPU validation", 480, 320}, &error);
        require(window != nullptr, error);
        std::cerr << "GPU: creating graphics context\n";
        auto graphics = GraphicsContext::create(*window, TINYENGINE_SHADER_DIRECTORY, &error);
        require(graphics != nullptr, error);
        graphics->setAssetRoot(std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets");
        ComputeDispatch compute{"BufferTransform.comp.hlsl.spv", {3, 1, 1}, {}};
        ComputeBuffer input{0, {130}}, output{1, std::vector<std::uint32_t>(130)};
        for (std::uint32_t index = 0; index < 130; ++index) input.Words.push_back(index);
        compute.Buffers = {input, output};
        std::vector<ComputeBuffer> readback;
        require(graphics->dispatch(compute, readback, &error), error);
        require(readback.size() == 2 && readback[0].Words == input.Words, "Compute input was changed");
        for (std::uint32_t index = 0; index < 130; ++index)
            require(readback[1].Words[index] == index * 2 + 1, "Compute readback has incorrect results");
        compute.Buffers[1].Binding = 0;
        require(!graphics->dispatch(compute, readback, &error), "Duplicate compute binding was accepted");
        require(readback[1].Words.back() == 259, "Failed compute changed the output");
        Scene scene{"GPU validation"};
        const Vec3 eye{5, 4, 7};
        const auto view = lookAt(eye, {0, 0, 0});
        const auto projection = perspectiveVulkan(0.8f, 1.5f, 0.1f, 100.0f);
        RenderSettings settings;
        settings.ShadowMapResolution = 512;
        const auto capture = [&] {
            window->pollEvents();
            require(graphics->render(scene, settings, view, projection, eye, nullptr, &error), error);
            FrameCapture result;
            require(graphics->CaptureFrame(result, &error), error);
            require(result.Width == window->width() && result.Height == window->height(), "Unexpected frame size");
            return result;
        };
        const auto empty = capture();
        const auto sun = scene.createEntity("Sun");
        scene.registry().get<TransformComponent>(sun).Rotation = Quat::fromEulerRadians({-0.9f, -0.6f, 0});
        auto& sunlight = scene.registry().emplace<LightComponent>(sun);
        sunlight.Type = LightType::Directional;
        sunlight.Intensity = 4.0f;
        const auto cube = scene.createEntity("Cube");
        scene.registry().emplace<MeshRendererComponent>(cube, "meshes/showcase.gltf");
        const auto ground = scene.createEntity("Ground");
        auto& transform = scene.registry().get<TransformComponent>(ground);
        transform.Translation = {0, -1.1f, 0};
        transform.Scale = {5, 0.1f, 5};
        scene.registry().emplace<MeshRendererComponent>(ground, "meshes/showcase.gltf");
        const auto lit = capture();
        require(difference(empty, lit) > 0.005, "Geometry did not change the image");
        savePpm(lit, std::filesystem::path(TINYENGINE_CAPTURE_DIRECTORY) / "gpu-lit.ppm");
        settings.Exposure = 0.2f;
        const auto dark = capture();
        require(difference(lit, dark) > 0.01, "Exposure did not affect the frame");
        settings.Exposure = 1.0f;
        settings.EnableShadows = false;
        const auto noShadows = capture();
        require(difference(lit, noShadows) > 0.00001, "Shadow pass did not affect the frame");
        settings.EnableShadows = true;
        settings.EnableSsao = false;
        const auto noAo = capture();
        require(difference(lit, noAo) > 0.000001, "SSAO did not affect the frame");
        settings.EnableSsao = true;
        scene.registry().get<MeshRendererComponent>(cube).Material = "materials/player.pbr";
        const auto blue = capture();
        require(difference(lit, blue) > 0.001, "Material override did not affect the frame");
        scene.registry().get<MeshRendererComponent>(cube).Material.clear();
        scene.registry().get<LightComponent>(sun).Intensity = 0;
        const auto unlit = capture();
        EnvironmentMap environment;
        environment.EquirectangularPath = "../outside.hdr";
        require(!graphics->setEnvironment(environment, &error), "Escaping HDRI path was accepted");
        environment.EquirectangularPath = "environments/studio.hdr";
        environment.CubemapResolution = 16;
        environment.IrradianceResolution = 8;
        environment.PrefilteredMipLevels = 5;
        require(graphics->setEnvironment(environment, &error), error);
        const auto hdri = capture();
        require(difference(unlit, hdri) > 0.001, "HDRI lighting did not change the scene");
        settings.EnableIbl = false;
        const auto noIbl = capture();
        require(difference(hdri, noIbl) > 0.001, "IBL switch did not affect HDR illumination");
        settings.EnableIbl = true;
        environment.EquirectangularPath = "environments/missing.hdr";
        require(!graphics->setEnvironment(environment, &error), "Missing HDRI was accepted");
        require(difference(hdri, capture()) < 0.000001, "Failed HDRI load changed the environment");
        require(graphics->setEnvironment({}, &error), error);
        require(difference(unlit, capture()) < 0.000001, "HDRI reset did not restore the default environment");
        environment.EquirectangularPath = "environments/studio_small_03_1k.hdr";
        require(graphics->setEnvironment(environment, &error), error);
        const auto polyHaven = capture();
        require(difference(unlit, polyHaven) > 0.001, "Poly Haven HDRI did not illuminate the scene");
        savePpm(polyHaven, std::filesystem::path(TINYENGINE_CAPTURE_DIRECTORY) / "gpu-polyhaven.ppm");
        require(graphics->setEnvironment({}, &error), error);
        const auto local = scene.createEntity("Local light");
        scene.registry().get<TransformComponent>(local).Translation = {2, 3, 3};
        auto& localLight = scene.registry().emplace<LightComponent>(local);
        localLight.Type = LightType::Point;
        localLight.Intensity = 80;
        localLight.Range = 15;
        const auto point = capture();
        require(difference(unlit, point) > 0.005, "Point light did not illuminate the scene");
        scene.registry().get<TransformComponent>(local).Rotation = Quat::fromEulerRadians({-0.6f, 0.5f, 0});
        scene.registry().get<LightComponent>(local).Type = LightType::Spot;
        const auto spot = capture();
        require(difference(unlit, spot) > 0.001, "Spot light did not illuminate the scene");
        require(difference(point, spot) > 0.0001, "Spot cone did not change point-light illumination");
        scene.registry().get<LightComponent>(local).Range = 0.1f;
        const auto outOfRange = capture();
        require(difference(unlit, outOfRange) < 0.000001, "Out-of-range light illuminated the scene");
        scene.registry().get<LightComponent>(local).Range = 15;
        scene.registry().get<LightComponent>(local).Type = LightType::Directional;
        const auto secondSun = capture();
        require(difference(unlit, secondSun) > 0.001, "Additional directional light did not illuminate the scene");
        scene.registry().get<LightComponent>(local).Intensity = -1;
        require(!graphics->render(scene, settings, view, projection, eye, nullptr, &error), "Invalid light was accepted");
        scene.registry().get<LightComponent>(local).Intensity = 1;
        scene.registry().get<MeshRendererComponent>(cube).Material = "../outside.pbr";
        require(!graphics->render(scene, settings, view, projection, eye, nullptr, &error), "Escaping material path was accepted");
        scene.registry().get<MeshRendererComponent>(cube).Material.clear();
        capture(); // Rejected frames must leave the context usable.
        std::cout << "GPU geometry, materials, directional/point/spot lights, HDR IBL, exposure, shadows, SSAO, compute buffers and readback passed\n";
        return 0;
    } catch (const std::exception& exception) {
        std::cerr << "GPU test failed: " << exception.what() << '\n';
        return 1;
    }
}

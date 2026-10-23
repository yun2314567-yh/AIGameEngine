#include "TinyEngine/Core.h"
#include "TinyEngine/Platform/Window.h"
#include "TinyEngine/Platform/Paths.h"
#include "TinyEngine/Renderer/GraphicsContext.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <system_error>
#include <utility>

namespace {
struct RuntimeOptions {
    std::string ScenePath;
    std::filesystem::path AssetRoot = ".";
    std::uint64_t FrameLimit = 0;
    bool HasFrameLimit = false;
    bool HasAssetRoot = false;
    bool Headless = false;
    std::filesystem::path ShaderDirectory;
    std::string EnvironmentPath;
};

void printUsage() {
    std::cerr << "Usage: TinyEngineRuntime [--scene] <scene.tiny> [--asset-root directory] [--frames N]\n"
                 "       TinyEngineRuntime [--frames N] [--headless] [--shader-dir directory] [--environment image.hdr]\n";
}

bool parseArguments(int argc, char** argv, RuntimeOptions& options) {
    bool hasScene = false;
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--scene") {
            if (hasScene || index + 1 >= argc || std::string(argv[index + 1]).empty()) {
                std::cerr << "--scene requires one scene path and may only be specified once\n";
                printUsage();
                return false;
            }
            options.ScenePath = argv[++index];
            hasScene = true;
        } else if (argument == "--asset-root") {
            if (options.HasAssetRoot || index + 1 >= argc || std::string(argv[index + 1]).empty()) {
                std::cerr << "--asset-root requires one directory and may only be specified once\n";
                printUsage();
                return false;
            }
            options.AssetRoot = argv[++index];
            options.HasAssetRoot = true;
        } else if (argument == "--headless") {
            options.Headless = true;
        } else if (argument == "--shader-dir" || argument == "--environment") {
            if (index + 1 >= argc || std::string(argv[index + 1]).empty()) {
                std::cerr << argument << " requires a path\n";
                return false;
            }
            if (argument == "--shader-dir") options.ShaderDirectory = argv[++index];
            else options.EnvironmentPath = argv[++index];
        } else if (argument == "--frames") {
            if (options.HasFrameLimit || index + 1 >= argc) {
                std::cerr << "--frames requires one positive integer and may only be specified once\n";
                printUsage();
                return false;
            }
            const std::string value = argv[++index];
            std::uint64_t frameLimit = 0;
            const auto [end, error] = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
            if (error != std::errc{} || end != value.data() + value.size() || frameLimit == 0) {
                std::cerr << "Invalid frame limit: " << value << " (expected a positive integer)\n";
                printUsage();
                return false;
            }
            options.FrameLimit = frameLimit;
            options.HasFrameLimit = true;
        } else if (!argument.empty() && argument.front() == '-') {
            std::cerr << "Unknown option: " << argument << '\n';
            printUsage();
            return false;
        } else if (argument.empty()) {
            std::cerr << "Scene path must not be empty\n";
            printUsage();
            return false;
        } else if (!hasScene) {
            options.ScenePath = argument;
            hasScene = true;
        } else {
            std::cerr << "Only one scene path may be specified\n";
            printUsage();
            return false;
        }
    }
    if (options.Headless && !options.HasFrameLimit) {
        std::cerr << "--headless requires --frames N\n";
        return false;
    }
    return true;
}

bool loadPackageDefaults(const std::filesystem::path& executable, RuntimeOptions& options) {
    if (!options.ScenePath.empty()) return true;
    const auto root = executable.parent_path();
    std::ifstream manifest(root / "tinyengine.manifest");
    if (!manifest) return true;
    std::string line;
    while (std::getline(manifest, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (!line.starts_with("scene=")) continue;
        const std::filesystem::path relative = line.substr(6);
        bool valid = !relative.empty() && !relative.has_root_path();
        for (const auto& part : relative) if (part == "..") valid = false;
        if (!valid) {
            std::cerr << "Invalid scene path in package manifest\n";
            return false;
        }
        options.ScenePath = (root / relative).string();
    }
    if (options.ScenePath.empty()) {
        std::cerr << "Package manifest has no main scene\n";
        return false;
    }
    if (!options.HasAssetRoot) options.AssetRoot = root;
    return true;
}

bool loadScene(const std::string& path, TinyEngine::Scene& scene) {
    if (path.empty()) return true;
    std::string text, error;
    if (!TinyEngine::ReadTextFile(path, 16 * 1024 * 1024, text, &error)) {
        std::cerr << "Could not read scene: " << error << '\n';
        return false;
    }
    TinyEngine::Scene loaded;
    if (!TinyEngine::Scene::tryDeserialize(text, loaded, error)) {
        std::cerr << "Invalid scene: " << error << '\n';
        return false;
    }
    scene = std::move(loaded);
    return true;
}
} // namespace

int main(int argc, char** argv) {
    RuntimeOptions options;
    if (!parseArguments(argc, argv, options)) return 2;
    const auto executable = TinyEngine::GetExecutablePath();
    if (!loadPackageDefaults(executable, options)) return 2;

    TinyEngine::EngineConfig config;
    config.ApplicationName = "TinyEngine Runtime";
    config.UseLuaScripts = true;
    TinyEngine::Engine engine{config};
    if (!loadScene(options.ScenePath, engine.scene())) return 2;
    std::string scriptError;
    if (!engine.loadScriptModules(options.AssetRoot, &scriptError)) {
        std::cerr << "Could not load scene scripts: " << scriptError << '\n';
        return 2;
    }
    std::cout << "TinyEngine Runtime - scene: " << engine.scene().name() << '\n';
    TinyEngine::AudioConfig audioConfig;
    audioConfig.Offline = options.Headless || !TinyEngine::GraphicsContext::available();
    if (TinyEngine::AudioSystem::IsAvailable() && !engine.StartAudio(options.AssetRoot, audioConfig, &scriptError)) {
        std::cerr << "Could not start scene audio: " << scriptError << '\n';
        return 2;
    }

    std::string windowError;
    auto window = options.Headless || !TinyEngine::GraphicsContext::available() ? nullptr
        : TinyEngine::Window::create({"TinyEngine Runtime"}, &windowError);
    if (!window && !options.Headless && TinyEngine::GraphicsContext::available()) {
        std::cerr << "Could not create runtime window: " << windowError << '\n';
        return 3;
    }
    if (!window && !options.HasFrameLimit) {
        std::cerr << "Could not create a runtime window: " << windowError
                  << ". Pass --frames N to run bounded headless execution.\n";
        return 3;
    }
    std::unique_ptr<TinyEngine::GraphicsContext> graphics;
    if (window) {
        if (options.ShaderDirectory.empty()) options.ShaderDirectory = TinyEngine::FindShaderDirectory(executable);
        graphics = TinyEngine::GraphicsContext::create(*window, options.ShaderDirectory, &windowError);
        if (!graphics) {
            std::cerr << "Could not create renderer: " << windowError << '\n';
            return 3;
        }
        graphics->setAssetRoot(options.AssetRoot);
        auto environment = engine.scene().Environment();
        if (!options.EnvironmentPath.empty()) environment.EquirectangularPath = options.EnvironmentPath;
        if (!graphics->setEnvironment(environment, &windowError)) {
            std::cerr << "Could not load environment: " << windowError << '\n';
            return 3;
        }
    }

    const auto update = [&engine](float deltaSeconds) {
        engine.update(deltaSeconds);
        if (!engine.AudioError().empty()) {
            std::cerr << "Scene audio update failed: " << engine.AudioError() << '\n';
            return false;
        }
        const auto* scripts = engine.luaScripts();
        if (scripts && !scripts->lastError().empty()) {
            std::cerr << "Scene script update failed: " << scripts->lastError() << '\n';
            return false;
        }
        return true;
    };

    std::uint64_t frameCount = 0;
    if (window) {
        using Clock = std::chrono::steady_clock;
        auto previousFrame = Clock::now();
        while (!window->shouldClose() && (!options.HasFrameLimit || frameCount < options.FrameLimit)) {
            window->pollEvents();
            if (window->shouldClose()) break;
            window->CaptureInput(engine.Input());

            const auto now = Clock::now();
            const float deltaSeconds = std::chrono::duration<float>(now - previousFrame).count();
            previousFrame = now;
            if (!update(std::clamp(deltaSeconds, 0.000001f, 0.1f))) return 4;
            TinyEngine::Vec3 position{6, 4, 8};
            auto view = TinyEngine::lookAt(position, {});
            float fov = 60, nearClip = 0.1f, farClip = 1000;
            for (auto entity : engine.scene().registry().entities()) {
                const auto* camera = engine.scene().registry().tryGet<TinyEngine::CameraComponent>(entity);
                const auto* transform = engine.scene().registry().tryGet<TinyEngine::TransformComponent>(entity);
                if (!camera || !camera->Primary || !transform) continue;
                position = transform->Translation;
                const auto rotation = transform->Rotation.normalized();
                view = TinyEngine::lookAt(position, position + rotation.rotate({0, 0, -1}), rotation.rotate({0, 1, 0}));
                fov = camera->FieldOfView; nearClip = camera->NearClip; farClip = camera->FarClip;
                break;
            }
            const float aspect = static_cast<float>(std::max(window->width(), 1u)) / static_cast<float>(std::max(window->height(), 1u));
            const auto projection = TinyEngine::perspectiveVulkan(fov * 0.01745329252f, aspect, nearClip, farClip);
            if (!graphics->render(engine.scene(), engine.scene().Rendering(), view, projection, position, nullptr, &windowError)) {
                std::cerr << "Rendering failed: " << windowError << '\n';
                return 5;
            }
            ++frameCount;
        }
    } else {
        std::cerr << "Running headless for "
                  << options.FrameLimit << " frames.\n";
        constexpr float HeadlessDeltaSeconds = 1.0f / 60.0f;
        while (frameCount < options.FrameLimit) {
            if (!update(HeadlessDeltaSeconds)) return 4;
            ++frameCount;
        }
    }

    std::cout << "Frames: " << frameCount << '\n';
    std::cout << "Entities: " << engine.scene().registry().size() << '\n';
    return 0;
}

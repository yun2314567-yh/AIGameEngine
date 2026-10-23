#include "TinyEngine/Core.h"
#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Editor/CommandProtocol.h"
#include "TinyEngine/Platform/Paths.h"

#if defined(TINYENGINE_WITH_EDITOR_UI)
#include "EditorUi.h"
#include "TinyEngine/Platform/Window.h"
#include "TinyEngine/Renderer/GraphicsContext.h"

#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#include <backends/imgui_impl_glfw.h>
#include <backends/imgui_impl_vulkan.h>
#include <imgui.h>
#endif

#include <algorithm>
#include <chrono>
#include <charconv>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

namespace {
enum class ArgumentResult { Success, Help, Error };

ArgumentResult parseArguments(int argc, char** argv, std::string& scenePath, std::string& commandPath,
                              std::filesystem::path& assetRoot, std::filesystem::path& runtimeBinary,
                              std::uint64_t& frameLimit) {
    for (int index = 1; index < argc; ++index) {
        const std::string argument = argv[index];
        if (argument == "--help") {
            std::cout << "Usage: TinyEngineEditor [--scene <scene.tiny>] [--asset-root <directory>] "
                         "[--runtime <executable>] [--commands <file|->] [--frames <positive count>]\n";
            return ArgumentResult::Help;
        }
        if (argument == "--scene" || argument == "--commands" || argument == "--asset-root" ||
            argument == "--runtime" || argument == "--frames") {
            if (index + 1 >= argc) {
                std::cerr << "Missing value for " << argument << '\n';
                return ArgumentResult::Error;
            }
            const std::string value = argv[++index];
            if (argument == "--scene") scenePath = value;
            else if (argument == "--commands") commandPath = value;
            else if (argument == "--asset-root") assetRoot = value;
            else if (argument == "--runtime") runtimeBinary = value;
            else {
                const auto parsed = std::from_chars(value.data(), value.data() + value.size(), frameLimit);
                if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size() || frameLimit == 0) {
                    std::cerr << "--frames requires a positive integer\n";
                    return ArgumentResult::Error;
                }
            }
            continue;
        }
        if (index == 1 && argc == 2) {
            scenePath = argument;
            continue;
        }
        std::cerr << "Usage: TinyEngineEditor [--scene <scene.tiny>] [--asset-root <directory>] "
                     "[--runtime <executable>] [--commands <file|->] [--frames <positive count>]\n";
        return ArgumentResult::Error;
    }
    return ArgumentResult::Success;
}

bool loadScene(const std::string& path, TinyEngine::Scene& scene) {
    if (path.empty()) return true;
    TinyEngine::EditorAutomation automation{scene};
    const auto result = automation.loadScene(path);
    if (!result) std::cerr << "Invalid scene: " << result.Error << '\n';
    return result.Succeeded;
}

#if defined(TINYENGINE_WITH_EDITOR_UI)
int runGraphicalEditor(TinyEngine::Engine& engine, const std::filesystem::path& scenePath,
                       const std::filesystem::path& assetRoot,
                       const std::filesystem::path& runtimeBinary, std::uint64_t frameLimit) {
    std::string error;
    auto window = TinyEngine::Window::create({"TinyEngine Editor"}, &error);
    if (!window) {
        std::cerr << "Could not create editor window: " << error << '\n';
        return 3;
    }

    auto graphics = TinyEngine::GraphicsContext::create(
        *window, TinyEngine::FindShaderDirectory(TinyEngine::GetExecutablePath()), &error);
    if (!graphics) {
        std::cerr << "Could not initialize editor graphics: " << error << '\n';
        return 3;
    }
    graphics->setAssetRoot(assetRoot);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    auto* nativeWindow = static_cast<GLFWwindow*>(window->nativeHandle());
    if (!ImGui_ImplGlfw_InitForVulkan(nativeWindow, true)) {
        std::cerr << "Could not initialize the ImGui GLFW backend\n";
        graphics.reset();
        ImGui::DestroyContext();
        return 3;
    }
    if (!graphics->initializeImGui(&error)) {
        std::cerr << "Could not initialize the ImGui Vulkan backend: " << error << '\n';
        graphics.reset();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        return 3;
    }

    TinyEngine::EditorUiOptions uiOptions;
    uiOptions.ScenePath = scenePath;
    uiOptions.AssetRoot = assetRoot;
    uiOptions.RuntimeBinary = runtimeBinary;
    TinyEngine::EditorUi ui{engine, uiOptions};
    TinyEngine::EnvironmentMap currentEnvironment;
    bool environmentLoaded = false;
    std::filesystem::path currentAssetRoot = assetRoot;
    using Clock = std::chrono::steady_clock;
    auto previousFrame = Clock::now();
    int result = 0;
    std::uint64_t frameCount = 0;

    while (!window->shouldClose() && !ui.closeRequested() && (frameLimit == 0 || frameCount < frameLimit)) {
        window->pollEvents();
        if (window->shouldClose()) break;
        if (window->width() == 0 || window->height() == 0) {
            previousFrame = Clock::now();
            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            continue;
        }

        const auto now = Clock::now();
        const float deltaSeconds = std::clamp(
            std::chrono::duration<float>(now - previousFrame).count(), 0.0f, 0.1f);
        previousFrame = now;

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        if (ui.inPlayMode() && !ImGui::GetIO().WantCaptureKeyboard) window->CaptureInput(engine.Input());
        else {
            engine.Input().BeginFrame();
            engine.Input().ReleaseAll();
        }
        ui.tick(deltaSeconds);
        ui.draw(deltaSeconds);
        const auto editedAssetRoot = ui.assetRoot();
        if (editedAssetRoot != currentAssetRoot) {
            currentAssetRoot = editedAssetRoot;
            graphics->setAssetRoot(currentAssetRoot);
            environmentLoaded = false;
        }
        const auto& environment = engine.scene().Environment();
        if (!environmentLoaded || environment != currentEnvironment) {
            if (!graphics->setEnvironment(environment, &error)) ui.ReportError("Environment: " + error);
            currentEnvironment = environment;
            environmentLoaded = true;
        }
        ImGui::Render();

        error.clear();
        if (!graphics->render(engine.scene(), engine.scene().Rendering(), ui.cameraView(), ui.cameraProjection(),
                              ui.cameraPosition(), ImGui::GetDrawData(), &error)) {
            std::cerr << "Editor rendering failed: " << error << '\n';
            result = 4;
            break;
        }
        ++frameCount;
    }

    // The graphics context owns the Vulkan ImGui backend and must release it
    // while the ImGui context is still alive.
    graphics.reset();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    std::cout << "Frames: " << frameCount << '\n';
    return result;
}
#endif
}

int main(int argc, char** argv) {
    TinyEngine::Engine engine{{"TinyEngine Editor", {0.0f, -9.81f, 0.0f}, true}};
    std::string scenePath;
    std::string commandPath;
    std::filesystem::path assetRoot = ".";
    std::filesystem::path runtimeBinary;
    std::uint64_t frameLimit = 0;
    const auto argumentResult = parseArguments(argc, argv, scenePath, commandPath, assetRoot, runtimeBinary, frameLimit);
    if (argumentResult == ArgumentResult::Help) return 0;
    if (argumentResult == ArgumentResult::Error || !loadScene(scenePath, engine.scene())) return 2;
    if (!commandPath.empty()) {
        TinyEngine::EditorCommandProtocol protocol{engine.scene(), assetRoot};
        if (commandPath == "-") return protocol.run(std::cin, std::cout, std::cerr) ? 0 : 1;
        std::ifstream commands(commandPath, std::ios::binary);
        if (!commands) {
            std::cerr << "Could not open command file: " << commandPath << '\n';
            return 2;
        }
        return protocol.run(commands, std::cout, std::cerr) ? 0 : 1;
    }
#if defined(TINYENGINE_WITH_EDITOR_UI)
    return runGraphicalEditor(engine, scenePath, assetRoot, runtimeBinary, frameLimit);
#else
#if defined(TINYENGINE_EDITOR_UI_REQUESTED)
    std::cerr << "This build requested the graphical editor but has no Vulkan/NVRHI editor backend; "
                 "running the command-line editor instead.\n";
#endif
    std::cout << "TinyEngine Editor - scene: " << engine.scene().name() << '\n';
    std::cout << "Entities: " << engine.scene().registry().size() << '\n';
    engine.update(1.0f / 60.0f);
    return 0;
#endif
}

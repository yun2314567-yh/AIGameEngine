#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Scripting.h"

#include <filesystem>
#include <iostream>

int main(int argc, char** argv) {
    using namespace TinyEngine;
    if (argc != 3) return 2;
    Scene scene;
    EditorAutomation automation(scene);
    const auto assets = std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets";
    auto result = automation.loadScene(assets / "scenes/FeatureShowcase.tiny");
    if (!result) { std::cerr << result.Error << '\n'; return 1; }
    if (!LuaScriptRuntime::available()) {
        for (const auto entity : scene.registry().entities()) scene.registry().remove<ScriptComponent>(entity);
    }
    RuntimeExportOptions options;
    options.RuntimeBinary = argv[1];
    options.OutputDirectory = argv[2];
    options.AssetRoot = assets;
    result = automation.exportRuntime(options);
    if (!result) { std::cerr << result.Error << '\n'; return 1; }
    std::cout << "Exported Feature Showcase\n";
    return 0;
}

#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Editor/CommandProtocol.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <limits>

int main() {
    using namespace TinyEngine;
    const auto root = std::filesystem::temp_directory_path() /
        ("tinyengine-automation-test-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code error;
    std::filesystem::remove_all(root, error);
    std::filesystem::create_directories(root, error);

    Scene scene{"Automation"};
    EditorAutomation automation{scene};
    const Entity cube = automation.createEntity("Cube");
    assert(automation.setTransform(cube, {1.0f, 2.0f, 3.0f}, {}, {2.0f, 2.0f, 2.0f}));
    assert(!automation.setTransform(cube, {std::numeric_limits<float>::infinity(), 0, 0}, {}, {1, 1, 1}));
    assert(!automation.setTransform(cube, {}, {0, 0, 0, 0}, {1, 1, 1}));
    assert(scene.registry().get<TransformComponent>(cube).Translation.x == 1);
    CameraComponent invalidCamera; invalidCamera.FarClip = invalidCamera.NearClip;
    assert(!automation.addCamera(cube, invalidCamera));
    RigidBodyComponent invalidBody; invalidBody.Mass = -1;
    assert(!automation.addRigidBody(cube, invalidBody));
    BoxColliderComponent invalidCollider; invalidCollider.HalfExtents.x = 0;
    assert(!automation.addBoxCollider(cube, invalidCollider));
    AudioSourceComponent invalidAudio; invalidAudio.Pitch = 0;
    assert(!automation.addAudioSource(cube, invalidAudio));
    MeshRendererComponent mesh;
    mesh.Mesh = "assets/mesh/cube.gltf";
    mesh.Material = "assets/materials/cube.mat";
    assert(automation.addMeshRenderer(cube, mesh).Succeeded);
    assert(automation.addBoxCollider(cube).Succeeded);
    assert(automation.attachScript(cube, "assets/scripts/cube.lua").Succeeded);
    AudioSourceComponent audio;
    audio.Clip = "assets/audio/hit.wav";
    assert(automation.addAudioSource(cube, audio).Succeeded);
    assert(!automation.addMeshRenderer(cube).Succeeded);

    const auto scenePath = root / "main.tiny";
    assert(automation.saveScene(scenePath).Succeeded);
    Scene loaded;
    EditorAutomation loadAutomation{loaded};
    assert(loadAutomation.loadScene(scenePath).Succeeded);
    assert(loaded.registry().size() == 1);

    const auto runtime = root / "TinyEngineRuntime.exe";
    { std::ofstream output(runtime, std::ios::binary); output << "runtime"; }
    const auto writeAsset = [](const std::filesystem::path& path, const std::string& contents) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream file(path, std::ios::binary);
        file << contents;
        assert(static_cast<bool>(file));
    };
    writeAsset(root / "assets/scripts/cube.lua", "return {}\n");
    writeAsset(root / "assets/audio/hit.wav", "audio-data");
    writeAsset(root / "assets/mesh/cube.gltf", R"({"asset":{"version":"2.0"},"buffers":[{"uri":"cube.bin","byteLength":4}],"images":[{"uri":"../textures/color%20map.png"}]})");
    writeAsset(root / "assets/mesh/cube.bin", "data");
    writeAsset(root / "assets/textures/color map.png", "image-data");
    const std::string materialData = "TINYENGINE_MATERIAL 1\nbase_color 1 1 1 1\nroughness 0.5\n";
    writeAsset(root / "assets/materials/cube.mat", materialData);
    writeAsset(root / "assets/environments/test.hdr", "hdr-data");
    EnvironmentMap environment;
    environment.EquirectangularPath = "assets/environments/test.hdr";
    assert(scene.SetEnvironment(environment));
    const auto package = root / "package";
    RuntimeExportOptions options;
    options.RuntimeBinary = runtime;
    options.OutputDirectory = package;
    options.AssetRoot = root;
    options.SceneFileName = "../escape.tiny";
    assert(!automation.exportRuntime(options).Succeeded);
    options.SceneFileName = "scene\nname.tiny";
    assert(!automation.exportRuntime(options).Succeeded);
    assert(!std::filesystem::exists(package));
    options.SceneFileName = "tinyengine.manifest";
    assert(!automation.exportRuntime(options).Succeeded);
    options.SceneFileName = "THIRD_PARTY_NOTICES.txt";
    assert(!automation.exportRuntime(options));
    options.SceneFileName = runtime.filename().string();
    assert(!automation.exportRuntime(options).Succeeded);
    options.SceneFileName = "main.tiny";
    const auto shaders = root / "compiled-shaders";
    for (const auto* name : {"Depth.vert", "Depth.frag", "Shadow.vert", "Shadow.frag", "Pbr.vert", "Pbr.frag",
                              "Ssao.comp", "SsaoBlur.comp", "BufferTransform.comp", "ToneMap.comp", "Present.vert", "Present.frag"})
        writeAsset(shaders / (std::string(name) + ".hlsl.spv"), "shader-data");
    options.ShaderDirectory = shaders;
    writeAsset(root / "libraries/private-library.dll", "library-data");
    options.RuntimeDependencies.push_back(root / "libraries/private-library.dll");
    options.OutputDirectory = root;
    assert(!automation.exportRuntime(options).Succeeded);
    options.OutputDirectory = package;
    assert(automation.exportRuntime(options).Succeeded);
    assert(std::filesystem::is_regular_file(package / "TinyEngineRuntime.exe"));
    assert(std::filesystem::is_regular_file(package / "main.tiny"));
    assert(std::filesystem::is_regular_file(package / "tinyengine.manifest"));
    std::ifstream noticesFile(package / "THIRD_PARTY_NOTICES.txt", std::ios::binary);
    const std::string notices((std::istreambuf_iterator<char>(noticesFile)), std::istreambuf_iterator<char>());
    assert(notices.find("NVIDIA") != std::string::npos);
    assert(notices.find("1994-2024 Lua.org") != std::string::npos);
    assert(std::filesystem::is_regular_file(package / "assets/scripts/cube.lua"));
    assert(std::filesystem::is_regular_file(package / "assets/audio/hit.wav"));
    assert(std::filesystem::is_regular_file(package / "assets/mesh/cube.gltf"));
    assert(std::filesystem::is_regular_file(package / "assets/mesh/cube.bin"));
    assert(std::filesystem::is_regular_file(package / "assets/textures/color map.png"));
    assert(std::filesystem::is_regular_file(package / "shaders/Pbr.vert.hlsl.spv"));
    assert(std::filesystem::is_regular_file(package / "private-library.dll"));
    assert(std::filesystem::is_regular_file(package / "assets/materials/cube.mat"));
    assert(std::filesystem::is_regular_file(package / "assets/environments/test.hdr"));
    std::ifstream manifestFile(package / "tinyengine.manifest", std::ios::binary);
    const std::string manifest((std::istreambuf_iterator<char>(manifestFile)), std::istreambuf_iterator<char>());
    assert(manifest.find("asset=assets/scripts/cube.lua\n") != std::string::npos);
    assert(manifest.find("asset=assets/audio/hit.wav\n") != std::string::npos);
    assert(manifest.find("asset=assets/mesh/cube.gltf\n") != std::string::npos);
    assert(manifest.find("asset=assets/mesh/cube.bin\n") != std::string::npos);
    assert(manifest.find("asset=assets/textures/color map.png\n") != std::string::npos);
    assert(manifest.find("asset=assets/materials/cube.mat\n") != std::string::npos);
    assert(manifest.find("asset=THIRD_PARTY_NOTICES.txt\n") != std::string::npos);

    const auto commandPackage = root / "command-package";
    EditorCommandProtocol exportProtocol{scene, root};
    std::ostringstream exportCommand;
    exportCommand << "export-runtime " << std::quoted(runtime.string()) << ' '
                  << std::quoted(commandPackage.string()) << '\n';
    std::istringstream exportCommands{exportCommand.str()};
    std::ostringstream exportOutput;
    std::ostringstream exportErrors;
    assert(exportProtocol.run(exportCommands, exportOutput, exportErrors));
    assert(exportErrors.str().empty());
    assert(std::filesystem::is_regular_file(commandPackage / "assets/mesh/cube.gltf"));
    assert(std::filesystem::is_regular_file(commandPackage / "assets/scripts/cube.lua"));

    const auto sentinel = package / "preserve-on-failure.txt";
    { std::ofstream file(sentinel, std::ios::binary); file << "old package"; }
    std::filesystem::remove(root / "assets/environments/test.hdr");
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    writeAsset(root / "assets/environments/test.hdr", "hdr-data");
    writeAsset(root / "assets/materials/cube.mat", "invalid-material");
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    writeAsset(root / "assets/materials/cube.mat", materialData);
    std::filesystem::remove(root / "assets/mesh/cube.bin");
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    writeAsset(root / "assets/mesh/cube.bin", "data");
    std::filesystem::remove(root / "assets/textures/color map.png");
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    writeAsset(root / "assets/textures/color map.png", "image-data");
    std::filesystem::remove(shaders / "SsaoBlur.comp.hlsl.spv");
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    writeAsset(shaders / "SsaoBlur.comp.hlsl.spv", "shader-data");
    scene.registry().get<AudioSourceComponent>(cube).Clip = "assets/audio/missing.wav";
    assert(!automation.exportRuntime(options));
    assert(std::filesystem::is_regular_file(sentinel));
    scene.registry().get<AudioSourceComponent>(cube).Clip = audio.Clip;
    scene.registry().get<MeshRendererComponent>(cube).Mesh = "../escape.glb";
    assert(!automation.exportRuntime(options).Succeeded);
    assert(std::filesystem::is_regular_file(sentinel));
    std::ifstream preservedManifestFile(package / "tinyengine.manifest", std::ios::binary);
    const std::string preservedManifest((std::istreambuf_iterator<char>(preservedManifestFile)), std::istreambuf_iterator<char>());
    assert(preservedManifest == manifest);

    Scene commandScene{"Command Protocol"};
    EditorCommandProtocol protocol{commandScene};
    const auto commandScenePath = root / "command-scene.tiny";
    const auto prefabPath = root / "player.prefab";
    std::ostringstream commandText;
    commandText << "create \"Player One\"\n"
                << "environment \"environments/studio.hdr\" 2 0.5 64 16 6\n"
                << "render-settings 1 1 1 0 1024 8 0.7 0.02 1.5 1 16 0.002\n"
                << "transform 1 1 2 3 0 0 0 1 1 1 1\n"
                << "add 1 camera 60 0.1 250 1\n"
                << "add 1 mesh-renderer \"player.glb\" \"player\" 1 1 1\n"
                << "add 1 script \"player.lua\"\n"
                << "save " << std::quoted(commandScenePath.string()) << '\n'
                << "inspect\n"
                << "rename 1 \"Hero\"\n"
                << "remove 1 camera\n"
                << "save-prefab 1 " << std::quoted(prefabPath.string()) << '\n'
                << "spawn-prefab " << std::quoted(prefabPath.string()) << '\n'
                << "inspect\n";
    std::istringstream commands{commandText.str()};
    std::ostringstream commandOutput;
    std::ostringstream commandErrors;
    assert(protocol.run(commands, commandOutput, commandErrors));
    assert(commandErrors.str().empty());
    assert(commandOutput.str().find("OK create entity=1") != std::string::npos);
    assert(commandOutput.str().find("SCENE name=\"Command Protocol\" entities=1") != std::string::npos);
    assert(commandOutput.str().find("components=\"transform,camera,script,mesh-renderer\"") != std::string::npos);
    assert(commandOutput.str().find("OK spawn-prefab entities=2") != std::string::npos);
    assert(commandScene.registry().size() == 2);
    assert(commandScene.Environment().Intensity == 2);
    assert(commandScene.Rendering().Exposure == 1.5f);
    assert(commandScene.Rendering().Tonemapper == ToneMapper::Reinhard);
    assert(!commandScene.registry().has<CameraComponent>(Entity{1}));
    assert(!commandScene.registry().has<CameraComponent>(Entity{2}));
    assert(commandScene.registry().get<TagComponent>(Entity{2}).Tag == "Hero");
    assert(commandScene.registry().has<MeshRendererComponent>(Entity{2}));

    const auto brokenPrefabPath = root / "broken.prefab";
    { std::ofstream output(brokenPrefabPath, std::ios::binary); output << "not a scene"; }
    std::ostringstream brokenPrefabText;
    brokenPrefabText << "spawn-prefab " << std::quoted(brokenPrefabPath.string()) << '\n';
    std::istringstream brokenPrefabCommand{brokenPrefabText.str()};
    assert(!protocol.run(brokenPrefabCommand, commandOutput, commandErrors));
    assert(commandScene.registry().size() == 2);

    std::ifstream commandSceneFile(commandScenePath, std::ios::binary);
    const std::string commandSceneText((std::istreambuf_iterator<char>(commandSceneFile)), std::istreambuf_iterator<char>());
    Scene commandSceneReloaded;
    std::string sceneError;
    assert(Scene::tryDeserialize(commandSceneText, commandSceneReloaded, sceneError));
    assert(commandSceneReloaded.registry().has<CameraComponent>(Entity{1}));
    assert(commandSceneReloaded.registry().has<MeshRendererComponent>(Entity{1}));
    assert(commandSceneReloaded.registry().has<ScriptComponent>(Entity{1}));
    assert(commandSceneReloaded.registry().get<TransformComponent>(Entity{1}).Translation.y == 2.0f);

    std::istringstream invalidCommands{"unknown-command\n"};
    assert(!protocol.run(invalidCommands, commandOutput, commandErrors));
    assert(commandErrors.str().find("ERROR line=1") != std::string::npos);

    std::filesystem::remove_all(root, error);
    return 0;
}

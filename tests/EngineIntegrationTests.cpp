#include "TinyEngine/Core.h"
#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Prefab.h"
#include "TinyEngine/Renderer/Renderer.h"
#include "TinyEngine/Renderer/Environment.h"
#include "TinyEngine/Scene.h"
#include "TinyEngine/Platform/Paths.h"

#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>

namespace {

void exerciseEngineWorkflow() {
    assert(std::filesystem::is_regular_file(TinyEngine::GetExecutablePath()));
    TinyEngine::Engine engine{{"Feature Showcase", {0.0f, -9.81f, 0.0f}}};
    auto& scene = engine.scene();
    const auto player = scene.createEntity("Player");
    scene.registry().emplace<TinyEngine::CameraComponent>(player).Primary = true;
    scene.registry().emplace<TinyEngine::RigidBodyComponent>(player);
    scene.registry().emplace<TinyEngine::BoxColliderComponent>(player);
    scene.registry().emplace<TinyEngine::ScriptComponent>(player, "player.lua");

    bool updated = false;
    engine.scripts().loadModule("player.lua", "function update(dt) end");
    engine.scripts().registerCallback("player.lua", [&updated](TinyEngine::ScriptContext&, float) { updated = true; });
    assert(engine.scripts().attach(scene, player, "player.lua"));
    engine.update(1.0f / 60.0f);
    assert(updated);

    const auto serialized = scene.serialize();
    const auto restored = TinyEngine::Scene::deserialize(serialized);
    assert(restored.registry().size() == 1);
    assert(restored.registry().tryGet<TinyEngine::CameraComponent>(restored.registry().entities().front()) != nullptr);

    const auto prefab = TinyEngine::Prefab::fromScene(scene);
    TinyEngine::Scene destination{"PrefabTarget"};
    assert(!prefab.instantiate(destination).empty());
    const auto invalid = scene.createEntity("Invalid body");
    scene.registry().emplace<TinyEngine::RigidBodyComponent>(invalid).Mass = -1;
    engine.update(1.0f / 60.0f);
    assert(!engine.physics().getBody(invalid));
}

void exerciseAutomationWorkflow() {
    using namespace TinyEngine;
    Scene scene{"Automation"};
    EditorAutomation automation(scene);
    const Entity entity = automation.createEntity("Agent-authored");
    assert(automation.setTransform(entity, {1.0f, 2.0f, 3.0f}, Quat{}, {2.0f, 2.0f, 2.0f}));
    assert(automation.addCamera(entity));
    assert(automation.addMeshRenderer(entity, MeshRendererComponent{"cube.glb", "metal", true, true, true}));
    assert(automation.addLight(entity));
    assert(automation.addRigidBody(entity));
    assert(automation.addBoxCollider(entity));
    assert(automation.addAudioSource(entity));
    assert(automation.attachScript(entity, "agent.lua"));
    assert(!automation.addCamera(entity));

    const auto root = std::filesystem::temp_directory_path() /
        ("tinyengine-engine-integration-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
    std::filesystem::create_directories(root, ignored);
    assert(!ignored);
    { std::ofstream file(root / "bounded.txt"); file << "12345"; }
    std::string bounded = "unchanged", readError;
    assert(!ReadTextFile(root / "bounded.txt", 4, bounded, &readError));
    assert(bounded == "unchanged" && !readError.empty());
    assert(ReadTextFile(root / "bounded.txt", 5, bounded, &readError) && bounded == "12345");
    assert(!ReadTextFile(root, 5, bounded, &readError));
    assert(automation.saveScene(root / "scene.tiny"));
    Scene loaded;
    std::string error;
    assert(Scene::tryDeserialize(scene.serialize(), loaded, error));
    assert(loaded.registry().size() == 1);
    assert(automation.loadScene(root / "scene.tiny"));
    assert(scene.registry().size() == 1);
    GltfAsset missingAsset;
    assert(!automation.importGltf(root / "missing.gltf", missingAsset));
    std::filesystem::remove_all(root, ignored);
}

void exerciseRendererWorkflow() {
    using namespace TinyEngine;
    NullRenderBackend backend;
    Renderer renderer(backend);
    RenderSettings settings;
    settings.EnableCompute = false;
    assert(renderer.setSettings(settings));
    const auto passes = renderer.buildPasses();
    assert(!passes.empty());
    assert(renderer.render({1280, 720, 1.0f / 60.0f, 0}));
    assert(backend.executedPassCount() == passes.size());
    assert(!renderer.render({0, 720, 1.0f / 60.0f, 1}));
    const Vec3 mapped = toneMap({4.0f, 1.0f, 0.0f}, 1.0f, ToneMapper::Aces);
    assert(mapped.x <= 1.0f && mapped.x > mapped.y && mapped.y > mapped.z);
    RendererCapabilities capabilities{true, true, true, 4096, {65535, 65535, 65535}};
    bool dispatched = false;
    VulkanNvrhiCallbacks callbacks;
    callbacks.BeginFrame = [](const RenderFrame&, std::string*) { return true; };
    callbacks.ExecutePass = [](const RenderPass&, const RenderFrame&, const RenderSettings&, std::string*) { return true; };
    callbacks.EndFrame = [] {};
    callbacks.DispatchCompute = [&](const ComputeDispatch& compute, std::string*) {
        dispatched = compute.Buffers.size() == 1 && compute.Buffers[0].Words == std::vector<std::uint32_t>{1, 2};
        return true;
    };
    VulkanNvrhiBackend computeBackend(capabilities, callbacks);
    Renderer computeRenderer(computeBackend);
    settings.EnableCompute = true;
    assert(computeRenderer.setSettings(settings));
    assert(computeRenderer.queueCompute({"test.spv", {1, 1, 1}, {{0, {1, 2}}}}));
    assert(computeRenderer.render({64, 64, 0.016f, 0}));
    assert(dispatched && computeRenderer.computeQueue().empty());
    FilteredEnvironment environment;
    std::vector<float> sky(8 * 4 * 4, 1);
    assert(FilterEnvironment(sky, 8, 4, 4, 3, 2, environment));
    assert(environment.SpecularMips.size() == 3 && environment.Irradiance.Pixels[0] > 0.99f);
}

void exerciseFeatureShowcase() {
    using namespace TinyEngine;
    const auto scenePath = std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets" / "scenes" / "FeatureShowcase.tiny";
    std::ifstream input(scenePath, std::ios::binary);
    assert(input);
    const std::string text((std::istreambuf_iterator<char>(input)), std::istreambuf_iterator<char>());
    Scene scene;
    std::string error;
    assert(Scene::tryDeserialize(text, scene, error));
    assert(scene.registry().size() == 6);
    bool hasCamera = false;
    bool hasScript = false;
    bool hasLight = false;
    bool hasAudio = false;
    scene.registry().each<CameraComponent>([&](Entity, const CameraComponent&) { hasCamera = true; });
    scene.registry().each<ScriptComponent>([&](Entity, const ScriptComponent&) { hasScript = true; });
    scene.registry().each<LightComponent>([&](Entity, const LightComponent&) { hasLight = true; });
    scene.registry().each<AudioSourceComponent>([&](Entity, const AudioSourceComponent&) { hasAudio = true; });
    assert(hasCamera && hasScript && hasLight && hasAudio);
    assert(scene.Environment().EquirectangularPath == "environments/studio_small_03_1k.hdr");
    assert(scene.Rendering().EnableSsao && scene.Rendering().EnableShadows && scene.Rendering().EnableIbl);
    assert(std::filesystem::is_regular_file(scenePath.parent_path().parent_path() / scene.Environment().EquirectangularPath));
    scene.registry().each<MeshRendererComponent>([&](Entity, const MeshRendererComponent& mesh) {
        if (mesh.Material.empty()) return;
        PbrMaterial material;
        assert(LoadPbrMaterialOverride(scenePath.parent_path().parent_path() / mesh.Material, material, &error));
    });
    if (LuaScriptRuntime::available()) {
        Engine engine{{"Showcase scripting", {}, true}};
        engine.scene() = std::move(scene);
        assert(engine.loadScriptModules(scenePath.parent_path().parent_path(), &error));
        engine.update(1.0f / 60.0f);
        assert(engine.luaScripts()->lastError().empty());
        engine.update(1.0f / 60.0f);
        assert(engine.luaScripts()->lastError().empty());
        assert(engine.scene().registry().size() == 6);
        // IDs 7 and 8 were created/spawned and destroyed through Lua.
        assert(!engine.scene().valid(Entity{7}) && !engine.scene().valid(Entity{8}));
        assert(engine.scene().createEntity("After probes").id == 9);
    }
}

void exerciseLuaWorkflow() {
    using namespace TinyEngine;
    Engine engine{{"Lua Workflow", {}, true}};
    auto& scene = engine.scene();
    const Entity actor = scene.createEntity("Actor");
    scene.registry().emplace<ScriptComponent>(actor, "actor.lua");
    const auto root = std::filesystem::temp_directory_path() / "tinyengine-lua-test";
    std::filesystem::create_directories(root);
    {
        std::ofstream script(root / "actor.lua", std::ios::binary);
        script << "return { on_attach = function(e) engine.set_translation(e, 1, 2, 3) end, "
                  "update = function(e, dt) local x,y,z = engine.get_translation(e); engine.set_translation(e, x+dt, y, z) end }";
    }
    std::string error;
    if (!LuaScriptRuntime::available()) {
        assert(!engine.loadScriptModules(root, &error));
        assert(!error.empty());
    } else {
        assert(engine.loadScriptModules(root, &error));
        engine.update(0.5f);
        assert(error.empty());
        assert(engine.luaScripts()->lastError().empty());
        assert(nearlyEqual(scene.registry().get<TransformComponent>(actor).Translation, {1.5f, 2, 3}));
        {
            std::ofstream script(root / "actor.lua", std::ios::binary | std::ios::trunc);
            script << "invalid lua syntax !";
        }
        assert(!engine.loadScriptModules(root, &error));
        engine.update(0.5f);
        assert(nearlyEqual(scene.registry().get<TransformComponent>(actor).Translation, {2, 2, 3}));

        LuaScriptRuntime lifecycle;
        assert(lifecycle.loadModule("self-destroy", "return { on_attach = function(e) engine.destroy_entity(e) end, update = function(e, dt) error('destroyed entity updated') end }"));
        const Entity ephemeral = scene.createEntity("Ephemeral");
        scene.registry().emplace<ScriptComponent>(ephemeral, "self-destroy");
        lifecycle.update(scene, 0.1f);
        assert(!scene.valid(ephemeral));
        assert(lifecycle.lastError().empty());

        LuaScriptRuntime invalidValues;
        const auto previous = scene.registry().get<TransformComponent>(actor).Translation;
        assert(invalidValues.loadModule("invalid-values", "return { update = function(e, dt) engine.set_translation(e, 9, 0/0, 3) end }"));
        assert(invalidValues.attach(scene, actor, "invalid-values"));
        invalidValues.update(scene, 0.1f);
        assert(!invalidValues.lastError().empty());
        assert(nearlyEqual(scene.registry().get<TransformComponent>(actor).Translation, previous));

        LuaScriptRuntime sharedRuntime;
        assert(sharedRuntime.loadModule("lifecycle", "return { on_attach = function(e) engine.set_translation(e, 7, 8, 9) end }"));
        Scene first{"First"}, second{"Second"};
        const auto firstActor = first.createEntity();
        const auto secondActor = second.createEntity();
        assert(firstActor == secondActor);
        first.registry().emplace<ScriptComponent>(firstActor, "lifecycle");
        second.registry().emplace<ScriptComponent>(secondActor, "lifecycle");
        sharedRuntime.update(first, 0.1f);
        sharedRuntime.update(second, 0.1f);
        assert(nearlyEqual(second.registry().get<TransformComponent>(secondActor).Translation, {7, 8, 9}));
        // Replacing a scene at the same address must also run on_attach.
        Scene replacement{"Replacement"};
        const auto replacementActor = replacement.createEntity();
        replacement.registry().emplace<ScriptComponent>(replacementActor, "lifecycle");
        second = std::move(replacement);
        sharedRuntime.update(second, 0.1f);
        assert(nearlyEqual(second.registry().get<TransformComponent>(replacementActor).Translation, {7, 8, 9}));
    }
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void exerciseKeyboardWorkflow() {
    using namespace TinyEngine;
    if (!LuaScriptRuntime::available()) return;
    Engine engine{{"Keyboard Workflow", {}, true}};
    const Entity actor = engine.scene().createEntity("Keyboard Actor");
    engine.scene().registry().emplace<ScriptComponent>(actor, "keyboard.lua");
    const auto root = std::filesystem::temp_directory_path() /
        ("tinyengine-keyboard-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(root);
    {
        std::ofstream script(root / "keyboard.lua", std::ios::binary);
        script << R"(return { update = function(e, dt)
            local x,y,z = engine.get_translation(e)
            if engine.key_down("Space") then x = x + 1 end
            if engine.key_pressed("Space") then y = y + 1 end
            if engine.key_released("Space") then z = z + 1 end
            engine.set_translation(e, x, y, z)
        end })";
    }
    std::string error;
    assert(engine.loadScriptModules(root, &error));
    const auto position = [&] { return engine.scene().registry().get<TransformComponent>(actor).Translation; };
    engine.update(0.1f);
    assert(nearlyEqual(position(), {}));
    engine.Input().BeginFrame();
    assert(engine.Input().SetKey(Key::Space, true));
    engine.update(0.1f);
    assert(nearlyEqual(position(), {1, 1, 0}));
    engine.Input().BeginFrame();
    engine.update(0.1f);
    assert(nearlyEqual(position(), {2, 1, 0}));
    engine.Input().BeginFrame();
    engine.Input().ReleaseAll();
    engine.update(0.1f);
    assert(nearlyEqual(position(), {2, 1, 1}));
    engine.Input().BeginFrame();
    engine.update(0.1f);
    assert(nearlyEqual(position(), {2, 1, 1}));
    for (const auto* key : {"unknown", "Space\\0ignored"}) {
        {
            std::ofstream script(root / "keyboard.lua", std::ios::binary | std::ios::trunc);
            script << "return { update = function(e, dt) engine.key_down(\"" << key << "\") end }";
        }
        assert(engine.loadScriptModules(root, &error));
        engine.update(0.1f);
        assert(engine.luaScripts()->lastError().find("Unknown key name") != std::string::npos);
        assert(nearlyEqual(position(), {2, 1, 1}));
    }
    LuaScriptRuntime withoutInput;
    assert(withoutInput.loadModule("no-input", R"(return { update = function(e, dt)
        assert(not engine.key_down("A"))
        assert(not engine.key_pressed("A"))
        assert(not engine.key_released("A"))
    end })"));
    assert(withoutInput.attach(engine.scene(), actor, "no-input"));
    withoutInput.update(engine.scene(), 0.1f);
    assert(withoutInput.lastError().empty());
    std::error_code ignored;
    std::filesystem::remove_all(root, ignored);
}

void exerciseAudioWorkflow() {
    using namespace TinyEngine;
    Engine engine;
    const auto entity = engine.scene().createEntity("Audio emitter");
    auto& source = engine.scene().registry().emplace<AudioSourceComponent>(entity);
    source.Clip = "audio/player-loop.wav";
    source.PlayOnStart = true;
    source.Loop = true;
    std::string error;
    const auto root = std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets";
    if (!AudioSystem::IsAvailable()) {
        assert(!engine.StartAudio(root, {true}, &error));
        return;
    }
    assert(engine.StartAudio(root, {true}, &error));
    engine.update(1.0f / 60.0f);
    assert(engine.AudioError().empty());
    assert(engine.Audio().ActiveVoiceCount() == 1);
    std::vector<float> pcm(4096);
    assert(engine.Audio().Mix(pcm, &error));
    float energy = 0;
    for (const auto sample : pcm) energy += sample * sample;
    assert(energy > 0.001f);
    source.Clip = "missing.wav";
    engine.update(1.0f / 60.0f);
    assert(!engine.AudioError().empty());
    source.Clip = "audio/player-loop.wav";
    engine.update(1.0f / 60.0f);
    assert(engine.AudioError().empty());
    engine.scene().destroyEntity(entity);
    engine.update(1.0f / 60.0f);
    assert(engine.Audio().ActiveVoiceCount() == 0);
    engine.StopAudio();
    assert(!engine.Audio().initialized());
}

} // namespace

void runEngineIntegrationTests() {
    exerciseEngineWorkflow();
    exerciseAutomationWorkflow();
    exerciseRendererWorkflow();
    exerciseFeatureShowcase();
    exerciseLuaWorkflow();
    exerciseKeyboardWorkflow();
    exerciseAudioWorkflow();
}

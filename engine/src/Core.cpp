#include "TinyEngine/Core.h"
#include "TinyEngine/Platform/Paths.h"

#include <cmath>
#include <fstream>
#include <iterator>
#include <set>

namespace TinyEngine {

Engine::Engine(EngineConfig config)
    : m_Config(std::move(config)), m_Scene(m_Config.ApplicationName), m_Scripts(std::make_unique<NativeScriptRuntime>()),
      m_Audio(std::make_unique<AudioSystem>()) {}

Engine::~Engine() = default;

bool Engine::StartAudio(const std::filesystem::path& assetRoot, const AudioConfig& config, std::string* error) {
    auto audio = std::make_unique<AudioSystem>();
    if (!audio->Initialize(config, error) || !audio->Synchronize(m_Scene, assetRoot, error)) return false;
    m_Audio = std::move(audio);
    m_AudioAssetRoot = assetRoot;
    m_AudioError.clear();
    return true;
}

void Engine::StopAudio() {
    m_Audio->shutdown();
    m_AudioError.clear();
}

bool Engine::loadScriptModules(const std::filesystem::path& assetRoot, std::string* error) {
    std::set<std::string> modules;
    m_Scene.registry().each<ScriptComponent>([&modules](Entity, const ScriptComponent& script) {
        if (script.Enabled) modules.insert(script.Module);
    });
    if (modules.empty()) {
        m_LuaScripts.reset();
        if (error) error->clear();
        return true;
    }
    if (!m_Config.UseLuaScripts || !LuaScriptRuntime::available()) {
        if (error) *error = "Scene contains enabled Lua scripts, but Lua scripting is unavailable";
        return false;
    }
    std::error_code pathError;
    const auto root = std::filesystem::weakly_canonical(assetRoot, pathError);
    if (pathError || !std::filesystem::is_directory(root, pathError)) {
        if (error) *error = "Script asset root is not a directory: " + assetRoot.string();
        return false;
    }
    auto runtime = std::make_unique<LuaScriptRuntime>(&m_Input);
    for (const auto& module : modules) {
        const std::filesystem::path relativePath{module};
        bool unsafe = module.empty() || relativePath.has_root_path();
        for (const auto& part : relativePath) if (part == "..") unsafe = true;
        const auto source = std::filesystem::weakly_canonical(root / relativePath, pathError);
        if (pathError || unsafe || source.lexically_relative(root).empty()) {
            if (error) *error = "Invalid script module path: " + module;
            return false;
        }
        const auto contained = source.lexically_relative(root);
        for (const auto& part : contained) if (part == "..") unsafe = true;
        if (unsafe || !std::filesystem::is_regular_file(source, pathError) || pathError) {
            if (error) *error = "Script module is missing or outside the asset root: " + module;
            return false;
        }
        std::string text;
        if (!ReadTextFile(source, 1024 * 1024, text, error)) return false;
        if (!runtime->loadModule(module, text)) {
            if (error) *error = "Could not load script " + module + ": " + runtime->lastError();
            return false;
        }
    }
    m_LuaScripts = std::move(runtime);
    if (error) error->clear();
    return true;
}

void Engine::update(float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    // Scripts run first so gameplay can update velocity/components before integration.
    if (m_Config.UseLuaScripts) {
        if (m_LuaScripts) m_LuaScripts->update(m_Scene, deltaSeconds);
    } else {
        m_Scripts->update(m_Scene, deltaSeconds);
    }
    m_Scene.registry().each<RigidBodyComponent>([this](Entity entity, const RigidBodyComponent& rigidBody) {
        PhysicsBody* body = m_Physics.getBody(entity);
        if (!body) body = m_Physics.addBody(entity, rigidBody);
        if (!body) return; // Invalid components are skipped without dereferencing null.
        body->Type = rigidBody.Type;
        body->Mass = rigidBody.Mass;
        body->Velocity = rigidBody.Velocity;
    });
    m_Physics.step(m_Scene, deltaSeconds, m_Config.Gravity);
    if (m_Audio->initialized()) m_Audio->Synchronize(m_Scene, m_AudioAssetRoot, &m_AudioError);
}

} // namespace TinyEngine

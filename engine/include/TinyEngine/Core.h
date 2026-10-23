#pragma once

#include "TinyEngine/Physics.h"
#include "TinyEngine/Prefab.h"
#include "TinyEngine/Scripting.h"
#include "TinyEngine/Audio/Audio.h"
#include "TinyEngine/Input.h"

#include <memory>
#include <filesystem>
#include <string>

namespace TinyEngine {

struct EngineConfig {
    std::string ApplicationName = "TinyEngine Application";
    Vec3 Gravity{0.0f, -9.81f, 0.0f};
    bool UseLuaScripts = false;
};

class Engine {
public:
    explicit Engine(EngineConfig config = {});
    ~Engine();
    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    void update(float deltaSeconds);
    // Replaces the loaded Lua modules only after all enabled scene scripts load.
    bool loadScriptModules(const std::filesystem::path& assetRoot, std::string* error = nullptr);
    // Main-thread only. Audio remains disabled unless explicitly started.
    // Failed initialization leaves the current audio service running.
    bool StartAudio(const std::filesystem::path& assetRoot, const AudioConfig& config = {}, std::string* error = nullptr);
    void StopAudio();
    [[nodiscard]] AudioSystem& Audio() { return *m_Audio; }
    [[nodiscard]] InputState& Input() { return m_Input; }
    [[nodiscard]] const InputState& Input() const { return m_Input; }
    [[nodiscard]] const std::string& AudioError() const { return m_AudioError; }
    [[nodiscard]] Scene& scene() { return m_Scene; }
    [[nodiscard]] PhysicsWorld& physics() { return m_Physics; }
    [[nodiscard]] NativeScriptRuntime& scripts() { return *m_Scripts; }
    [[nodiscard]] LuaScriptRuntime* luaScripts() { return m_LuaScripts.get(); }
    [[nodiscard]] const EngineConfig& config() const { return m_Config; }

private:
    EngineConfig m_Config;
    Scene m_Scene;
    PhysicsWorld m_Physics;
    InputState m_Input;
    std::unique_ptr<NativeScriptRuntime> m_Scripts;
    std::unique_ptr<LuaScriptRuntime> m_LuaScripts;
    std::unique_ptr<AudioSystem> m_Audio;
    std::filesystem::path m_AudioAssetRoot;
    std::string m_AudioError;
};

} // namespace TinyEngine

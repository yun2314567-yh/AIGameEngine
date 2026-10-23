#pragma once

#include "TinyEngine/Scene.h"
#include "TinyEngine/Input.h"

#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>

struct lua_State;

namespace TinyEngine {

class ScriptContext {
public:
    explicit ScriptContext(Scene& scene) : m_Scene(scene) {}
    [[nodiscard]] Scene& scene() const { return m_Scene; }
    Entity createEntity(std::string name = "Entity") { return m_Scene.createEntity(std::move(name)); }
    bool destroyEntity(Entity entity) { return m_Scene.destroyEntity(entity); }

private:
    Scene& m_Scene;
};

class IScriptRuntime {
public:
    using UpdateCallback = std::function<void(ScriptContext&, float)>;
    virtual ~IScriptRuntime() = default;
    virtual bool loadModule(std::string_view moduleName, std::string_view source) = 0;
    virtual bool unloadModule(std::string_view moduleName) = 0;
    virtual bool attach(Scene& scene, Entity entity, std::string_view moduleName) = 0;
    virtual void update(Scene& scene, float deltaSeconds) = 0;
};

class NativeScriptRuntime final : public IScriptRuntime {
public:
    bool loadModule(std::string_view moduleName, std::string_view source) override;
    bool unloadModule(std::string_view moduleName) override;
    bool attach(Scene& scene, Entity entity, std::string_view moduleName) override;
    void update(Scene& scene, float deltaSeconds) override;
    void registerCallback(std::string moduleName, UpdateCallback callback);

private:
    struct Module { std::string Source; UpdateCallback Callback; };
    std::unordered_map<std::string, Module> m_Modules;
};

// Lua integration is optional. When TINYENGINE_WITH_LUA is disabled the class
// remains linkable and reports a descriptive error instead of requiring Lua.
// All methods are single-threaded. Switching scenes resets lifecycle tracking;
// on_attach runs again when returning to a previous scene. Modules stay loaded.
class LuaScriptRuntime final : public IScriptRuntime {
public:
    [[nodiscard]] static bool available();
    // The optional input snapshot must outlive this runtime. Null means no keys.
    explicit LuaScriptRuntime(const InputState* input = nullptr);
    ~LuaScriptRuntime() override;
    LuaScriptRuntime(const LuaScriptRuntime&) = delete;
    LuaScriptRuntime& operator=(const LuaScriptRuntime&) = delete;

    bool loadModule(std::string_view moduleName, std::string_view source) override;
    bool unloadModule(std::string_view moduleName) override;
    bool attach(Scene& scene, Entity entity, std::string_view moduleName) override;
    void update(Scene& scene, float deltaSeconds) override;
    [[nodiscard]] std::string lastError() const { return m_LastError; }

private:
    void selectScene(Scene& scene);
    bool callModuleFunction(Scene& scene, Entity entity, std::string_view moduleName, std::string_view functionName, float deltaSeconds, bool includeDelta);
    void setError(std::string message);
    static int luaCreateEntity(lua_State* state);
    static int luaDestroyEntity(lua_State* state);
    static int luaGetTranslation(lua_State* state);
    static int luaSetTranslation(lua_State* state);
    static int luaGetVelocity(lua_State* state);
    static int luaSetVelocity(lua_State* state);
    static int luaSpawnPrefab(lua_State* state);
    static int LuaKeyDown(lua_State* state);
    static int LuaKeyPressed(lua_State* state);
    static int LuaKeyReleased(lua_State* state);

    lua_State* m_State = nullptr;
    const InputState* m_Input = nullptr;
    Scene* m_ActiveScene = nullptr;
    Entity m_ActiveEntity{};
    std::unordered_map<std::string, int> m_Modules;
    std::unordered_map<Entity, std::string, EntityHash> m_AttachedModules;
    std::weak_ptr<const void> m_AttachedScene;
    std::string m_LastError;
};

} // namespace TinyEngine

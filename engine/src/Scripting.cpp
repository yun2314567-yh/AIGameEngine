#include "TinyEngine/Scripting.h"

#include "TinyEngine/Prefab.h"

#include <unordered_map>
#include <cmath>
#include <limits>
#include <iterator>
#include <utility>
#include <vector>

#ifdef TINYENGINE_WITH_LUA
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
#endif

namespace TinyEngine {

bool NativeScriptRuntime::loadModule(std::string_view moduleName, std::string_view source) {
    m_Modules[std::string(moduleName)].Source = source;
    return true;
}

bool NativeScriptRuntime::unloadModule(std::string_view moduleName) { return m_Modules.erase(std::string(moduleName)) != 0; }

bool NativeScriptRuntime::attach(Scene& scene, Entity entity, std::string_view moduleName) {
    if (!scene.valid(entity) || !m_Modules.contains(std::string(moduleName))) return false;
    auto& script = scene.registry().emplace<ScriptComponent>(entity);
    script.Module = moduleName;
    script.Enabled = true;
    return true;
}

void NativeScriptRuntime::update(Scene& scene, float deltaSeconds) {
    if (!std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    Registry::MutationScope mutations(scene.registry());
    const std::vector<Entity> entities = scene.registry().entities();
    for (const Entity entity : entities) {
        const auto* script = scene.registry().tryGet<ScriptComponent>(entity);
        if (!script || !script->Enabled) continue;
        const auto iterator = m_Modules.find(script->Module);
        if (iterator != m_Modules.end() && iterator->second.Callback) {
            const UpdateCallback callback = iterator->second.Callback;
            ScriptContext context(scene);
            callback(context, deltaSeconds);
        }
    }
}

void NativeScriptRuntime::registerCallback(std::string moduleName, UpdateCallback callback) {
    m_Modules[std::move(moduleName)].Callback = std::move(callback);
}

bool LuaScriptRuntime::available() {
#ifdef TINYENGINE_WITH_LUA
    return true;
#else
    return false;
#endif
}

namespace {

#ifdef TINYENGINE_WITH_LUA
LuaScriptRuntime* runtimeFromLua(lua_State* state) {
    return static_cast<LuaScriptRuntime*>(lua_touserdata(state, lua_upvalueindex(1)));
}

Entity entityFromLua(lua_State* state, int index) {
    const lua_Integer value = luaL_checkinteger(state, index);
    if (value <= 0 || value > 0xffffffffLL) return {};
    return Entity{static_cast<std::uint32_t>(value)};
}

float finiteFloatFromLua(lua_State* state, int index) {
    const lua_Number value = luaL_checknumber(state, index);
    if (!std::isfinite(value) || std::abs(value) > std::numeric_limits<float>::max()) {
        luaL_argerror(state, index, "Expected a finite float");
        return 0.0f;
    }
    return static_cast<float>(value);
}

Key keyFromLua(lua_State* state) {
    std::size_t length = 0;
    const char* name = luaL_checklstring(state, 1, &length);
    Key key = Key::Count;
    if (!InputState::KeyFromName(std::string_view(name, length), key))
        luaL_argerror(state, 1, "Unknown key name");
    return key;
}

bool luaError(lua_State* state, std::string& error) {
    if (lua_isstring(state, -1)) error = lua_tostring(state, -1);
    else error = "Lua operation failed";
    return false;
}
#endif

} // namespace

LuaScriptRuntime::LuaScriptRuntime(const InputState* input) : m_Input(input) {
#ifdef TINYENGINE_WITH_LUA
    m_State = luaL_newstate();
    if (!m_State) {
        setError("Lua state allocation failed");
        return;
    }
    luaL_openlibs(m_State);
    lua_newtable(m_State);
    const auto addBinding = [this](const char* name, lua_CFunction function) {
        lua_pushlightuserdata(m_State, this);
        lua_pushcclosure(m_State, function, 1);
        lua_setfield(m_State, -2, name);
    };
    addBinding("create_entity", &LuaScriptRuntime::luaCreateEntity);
    addBinding("destroy_entity", &LuaScriptRuntime::luaDestroyEntity);
    addBinding("get_translation", &LuaScriptRuntime::luaGetTranslation);
    addBinding("set_translation", &LuaScriptRuntime::luaSetTranslation);
    addBinding("get_velocity", &LuaScriptRuntime::luaGetVelocity);
    addBinding("set_velocity", &LuaScriptRuntime::luaSetVelocity);
    addBinding("spawn_prefab", &LuaScriptRuntime::luaSpawnPrefab);
    addBinding("key_down", &LuaScriptRuntime::LuaKeyDown);
    addBinding("key_pressed", &LuaScriptRuntime::LuaKeyPressed);
    addBinding("key_released", &LuaScriptRuntime::LuaKeyReleased);
    lua_setglobal(m_State, "engine");
#else
    setError("Lua support was not enabled when TinyEngine was built");
#endif
}

LuaScriptRuntime::~LuaScriptRuntime() {
#ifdef TINYENGINE_WITH_LUA
    if (m_State) lua_close(m_State);
#endif
}

void LuaScriptRuntime::setError(std::string message) { m_LastError = std::move(message); }

void LuaScriptRuntime::selectScene(Scene& scene) {
    const auto identity = scene.RuntimeIdentity();
    if (m_AttachedScene.expired() || m_AttachedScene.lock() != identity.lock()) {
        m_AttachedModules.clear();
        m_AttachedScene = identity;
    }
}

bool LuaScriptRuntime::loadModule(std::string_view moduleName, std::string_view source) {
#ifdef TINYENGINE_WITH_LUA
    if (!m_State) return false;
    const std::string name(moduleName);
    const int stackBase = lua_gettop(m_State);
    if (luaL_loadbuffer(m_State, source.data(), source.size(), name.c_str()) != LUA_OK) {
        const bool result = luaError(m_State, m_LastError);
        lua_settop(m_State, stackBase);
        return result;
    }
    if (lua_pcall(m_State, 0, 1, 0) != LUA_OK) {
        const bool result = luaError(m_State, m_LastError);
        lua_settop(m_State, stackBase);
        return result;
    }
    if (!lua_istable(m_State, -1)) {
        lua_pop(m_State, 1);
        lua_newtable(m_State);
    }
    const int reference = luaL_ref(m_State, LUA_REGISTRYINDEX);
    const auto existing = m_Modules.find(name);
    if (existing != m_Modules.end()) {
        luaL_unref(m_State, LUA_REGISTRYINDEX, existing->second);
        existing->second = reference;
    } else {
        m_Modules.emplace(name, reference);
    }
    for (auto attached = m_AttachedModules.begin(); attached != m_AttachedModules.end();) {
        attached = attached->second == name ? m_AttachedModules.erase(attached) : std::next(attached);
    }
    m_LastError.clear();
    return true;
#else
    (void)moduleName;
    (void)source;
    setError("Lua support was not enabled when TinyEngine was built");
    return false;
#endif
}

bool LuaScriptRuntime::unloadModule(std::string_view moduleName) {
#ifdef TINYENGINE_WITH_LUA
    if (!m_State) return false;
    const auto iterator = m_Modules.find(std::string(moduleName));
    if (iterator == m_Modules.end()) return false;
    luaL_unref(m_State, LUA_REGISTRYINDEX, iterator->second);
    m_Modules.erase(iterator);
    for (auto attached = m_AttachedModules.begin(); attached != m_AttachedModules.end();) {
        attached = attached->second == moduleName ? m_AttachedModules.erase(attached) : std::next(attached);
    }
    m_LastError.clear();
    return true;
#else
    (void)moduleName;
    setError("Lua support was not enabled when TinyEngine was built");
    return false;
#endif
}

bool LuaScriptRuntime::attach(Scene& scene, Entity entity, std::string_view moduleName) {
#ifdef TINYENGINE_WITH_LUA
    if (!m_State || !scene.valid(entity) || !m_Modules.contains(std::string(moduleName))) return false;
    selectScene(scene);
    const std::string name(moduleName);
    Registry::MutationScope mutations(scene.registry());
    auto& script = scene.registry().emplace<ScriptComponent>(entity);
    script.Module = name;
    script.Enabled = true;
    if (!callModuleFunction(scene, entity, name, "on_attach", 0.0f, false)) return false;
    if (scene.valid(entity)) m_AttachedModules.insert_or_assign(entity, name);
    return true;
#else
    (void)scene;
    (void)entity;
    (void)moduleName;
    setError("Lua support was not enabled when TinyEngine was built");
    return false;
#endif
}

void LuaScriptRuntime::update(Scene& scene, float deltaSeconds) {
#ifdef TINYENGINE_WITH_LUA
    if (!m_State || !std::isfinite(deltaSeconds) || deltaSeconds <= 0.0f) return;
    selectScene(scene);
    Registry::MutationScope mutations(scene.registry());
    const auto entities = scene.registry().entities();
    for (auto attached = m_AttachedModules.begin(); attached != m_AttachedModules.end();) {
        attached = !scene.valid(attached->first) ? m_AttachedModules.erase(attached) : std::next(attached);
    }
    for (const Entity entity : entities) {
        const auto* script = scene.registry().tryGet<ScriptComponent>(entity);
        if (!script || !script->Enabled || !m_Modules.contains(script->Module)) continue;
        const std::string module = script->Module;
        const auto attached = m_AttachedModules.find(entity);
        if (attached == m_AttachedModules.end() || attached->second != module) {
            if (!attach(scene, entity, module)) return;
        }
        script = scene.registry().tryGet<ScriptComponent>(entity);
        if (!script || !script->Enabled || script->Module != module || scene.registry().pendingDestruction(entity)) continue;
        if (!callModuleFunction(scene, entity, module, "update", deltaSeconds, true)) return;
    }
#else
    (void)scene;
    (void)deltaSeconds;
#endif
}

bool LuaScriptRuntime::callModuleFunction(Scene& scene, Entity entity, std::string_view moduleName, std::string_view functionName, float deltaSeconds, bool includeDelta) {
#ifdef TINYENGINE_WITH_LUA
    const auto module = m_Modules.find(std::string(moduleName));
    if (!m_State || module == m_Modules.end()) return false;
    const int stackBase = lua_gettop(m_State);
    m_ActiveScene = &scene;
    m_ActiveEntity = entity;
    lua_rawgeti(m_State, LUA_REGISTRYINDEX, module->second);
    lua_getfield(m_State, -1, std::string(functionName).c_str());
    if (!lua_isfunction(m_State, -1)) {
        lua_settop(m_State, stackBase);
        m_ActiveScene = nullptr;
        return true; // Lifecycle hooks are optional.
    }
    lua_pushinteger(m_State, static_cast<lua_Integer>(entity.id));
    if (includeDelta) lua_pushnumber(m_State, static_cast<lua_Number>(deltaSeconds));
    const int argumentCount = includeDelta ? 2 : 1;
    if (lua_pcall(m_State, argumentCount, 0, 0) != LUA_OK) {
        luaError(m_State, m_LastError);
        lua_settop(m_State, stackBase);
        m_ActiveScene = nullptr;
        return false;
    }
    lua_settop(m_State, stackBase);
    m_ActiveScene = nullptr;
    m_LastError.clear();
    return true;
#else
    (void)scene;
    (void)entity;
    (void)moduleName;
    (void)functionName;
    (void)deltaSeconds;
    (void)includeDelta;
    return false;
#endif
}

int LuaScriptRuntime::luaCreateEntity(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    const char* name = luaL_optstring(state, 1, "Entity");
    lua_pushinteger(state, static_cast<lua_Integer>(runtime->m_ActiveScene->createEntity(name).id));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaDestroyEntity(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    lua_pushboolean(state, runtime->m_ActiveScene->destroyEntity(entityFromLua(state, 1)));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaGetTranslation(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    const auto* transform = runtime->m_ActiveScene->registry().tryGet<TransformComponent>(entityFromLua(state, 1));
    if (!transform) return luaL_error(state, "Entity has no transform");
    lua_pushnumber(state, transform->Translation.x);
    lua_pushnumber(state, transform->Translation.y);
    lua_pushnumber(state, transform->Translation.z);
    return 3;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaSetTranslation(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    auto* transform = runtime->m_ActiveScene->registry().tryGet<TransformComponent>(entityFromLua(state, 1));
    if (!transform) return luaL_error(state, "Entity has no transform");
    const Vec3 value{finiteFloatFromLua(state, 2), finiteFloatFromLua(state, 3), finiteFloatFromLua(state, 4)};
    transform->Translation = value;
    return 0;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaGetVelocity(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    const auto* body = runtime->m_ActiveScene->registry().tryGet<RigidBodyComponent>(entityFromLua(state, 1));
    if (!body) return luaL_error(state, "Entity has no rigid body");
    lua_pushnumber(state, body->Velocity.x);
    lua_pushnumber(state, body->Velocity.y);
    lua_pushnumber(state, body->Velocity.z);
    return 3;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaSetVelocity(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    auto* body = runtime->m_ActiveScene->registry().tryGet<RigidBodyComponent>(entityFromLua(state, 1));
    if (!body) return luaL_error(state, "Entity has no rigid body");
    const Vec3 value{finiteFloatFromLua(state, 2), finiteFloatFromLua(state, 3), finiteFloatFromLua(state, 4)};
    body->Velocity = value;
    return 0;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::luaSpawnPrefab(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    auto* runtime = runtimeFromLua(state);
    if (!runtime || !runtime->m_ActiveScene) return luaL_error(state, "No active scene");
    const char* serialized = luaL_checkstring(state, 1);
    const auto entities = Prefab{serialized}.instantiate(*runtime->m_ActiveScene);
    if (entities.empty()) {
        lua_pushnil(state);
        return 1;
    }
    lua_pushinteger(state, static_cast<lua_Integer>(entities.front().id));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::LuaKeyDown(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    const auto* runtime = runtimeFromLua(state);
    const Key key = keyFromLua(state);
    lua_pushboolean(state, runtime && runtime->m_Input && runtime->m_Input->IsKeyDown(key));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::LuaKeyPressed(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    const auto* runtime = runtimeFromLua(state);
    const Key key = keyFromLua(state);
    lua_pushboolean(state, runtime && runtime->m_Input && runtime->m_Input->WasKeyPressed(key));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

int LuaScriptRuntime::LuaKeyReleased(lua_State* state) {
#ifdef TINYENGINE_WITH_LUA
    const auto* runtime = runtimeFromLua(state);
    const Key key = keyFromLua(state);
    lua_pushboolean(state, runtime && runtime->m_Input && runtime->m_Input->WasKeyReleased(key));
    return 1;
#else
    (void)state;
    return 0;
#endif
}

} // namespace TinyEngine

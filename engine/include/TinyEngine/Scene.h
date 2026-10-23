#pragma once

#include "TinyEngine/ECS.h"
#include "TinyEngine/Components.h"
#include "TinyEngine/Math.h"
#include "TinyEngine/Renderer/Components.h"
#include "TinyEngine/Renderer/Renderer.h"

#include <string>
#include <string_view>
#include <vector>
#include <memory>

namespace TinyEngine {

struct TagComponent {
    std::string Tag;
    explicit TagComponent(std::string tag = "Entity") : Tag(std::move(tag)) {}
};

struct TransformComponent {
    Vec3 Translation{};
    Quat Rotation{};
    Vec3 Scale{1.0f, 1.0f, 1.0f};
    [[nodiscard]] Mat4 localMatrix() const { return Mat4::trs(Translation, Rotation, Scale); }
};

struct CameraComponent {
    float FieldOfView = 45.0f;
    float NearClip = 0.01f;
    float FarClip = 1000.0f;
    bool Primary = false;
};

struct ScriptComponent {
    std::string Module;
    bool Enabled = true;
};

class Scene {
public:
    Scene() : m_Name("Untitled") {}
    explicit Scene(std::string name) : m_Name(std::move(name)) {}
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    Scene(Scene&&) noexcept = default;
    Scene& operator=(Scene&&) noexcept = default;

    Entity createEntity(std::string name = "Entity");
    bool destroyEntity(Entity entity) { return m_Registry.destroy(entity); }
    [[nodiscard]] bool valid(Entity entity) const { return m_Registry.valid(entity); }
    [[nodiscard]] const std::string& name() const { return m_Name; }
    void setName(std::string name) { m_Name = std::move(name); }
    [[nodiscard]] Registry& registry() { return m_Registry; }
    [[nodiscard]] const Registry& registry() const { return m_Registry; }
    // Identifies this scene's contents across moves/replacement. Runtime services
    // may keep a weak token without extending the scene's lifetime. Owning thread.
    [[nodiscard]] std::weak_ptr<const void> RuntimeIdentity() const { return m_RuntimeIdentity; }
    [[nodiscard]] const RenderSettings& Rendering() const { return m_RenderSettings; }
    [[nodiscard]] const EnvironmentMap& Environment() const { return m_Environment; }
    // Owning thread only. Invalid input leaves the scene unchanged.
    bool SetRendering(const RenderSettings& settings, std::string* error = nullptr);
    bool SetEnvironment(const EnvironmentMap& environment, std::string* error = nullptr);

    std::string serialize() const;
    static Scene deserialize(std::string_view text);
    static bool tryDeserialize(std::string_view text, Scene& destination, std::string& error);
    bool save(const std::string& path) const;
    static Scene load(const std::string& path);

private:
    std::string m_Name;
    Registry m_Registry;
    RenderSettings m_RenderSettings;
    EnvironmentMap m_Environment;
    std::shared_ptr<const void> m_RuntimeIdentity = std::make_shared<const char>(char{0});
};

} // namespace TinyEngine

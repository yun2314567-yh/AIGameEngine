#pragma once

#include "TinyEngine/Assets/GltfImporter.h"
#include "TinyEngine/Editor/EngineHost.h"

#include <filesystem>
#include <string>
#include <vector>

namespace TinyEngine {

struct AutomationResult {
    bool Succeeded = false;
    std::string Error;

    explicit operator bool() const { return Succeeded; }
    static AutomationResult success() { return {true, {}}; }
    static AutomationResult failure(std::string message) { return {false, std::move(message)}; }
};

struct RuntimeExportOptions {
    std::filesystem::path RuntimeBinary;
    std::filesystem::path OutputDirectory;
    std::string SceneFileName = "main.tiny";
    // Relative component paths are resolved here. An empty path uses current_path().
    std::filesystem::path AssetRoot;
    // Empty discovers shaders beside RuntimeBinary or in this engine build.
    // Vulkan builds require the complete compiled renderer shader set.
    std::filesystem::path ShaderDirectory;
    // Private shared libraries are copied beside the executable. The caller
    // supplies the runtime's non-system libraries for its target platform.
    std::vector<std::filesystem::path> RuntimeDependencies;
};

// Stable, input-free operations for editor agents and build automation.
class EditorAutomation {
public:
    explicit EditorAutomation(Scene& scene) : m_Scene(scene) {}

    [[nodiscard]] Entity createEntity(std::string name = "Entity") { return m_Scene.createEntity(std::move(name)); }
    AutomationResult destroyEntity(Entity entity);
    AutomationResult setTransform(Entity entity, const Vec3& translation, const Quat& rotation, const Vec3& scale);
    AutomationResult addCamera(Entity entity, const CameraComponent& component = {});
    AutomationResult addMeshRenderer(Entity entity, const MeshRendererComponent& component = {});
    AutomationResult addLight(Entity entity, const LightComponent& component = {});
    AutomationResult addRigidBody(Entity entity, const RigidBodyComponent& component = {});
    AutomationResult addBoxCollider(Entity entity, const BoxColliderComponent& component = {});
    AutomationResult addAudioSource(Entity entity, const AudioSourceComponent& component = {});
    AutomationResult attachScript(Entity entity, std::string module);

    AutomationResult importGltf(const std::filesystem::path& path, GltfAsset& asset,
                                const GltfImportOptions& options = {});
    AutomationResult saveScene(const std::filesystem::path& path) const;
    AutomationResult loadScene(const std::filesystem::path& path);

    // Stages a complete package and replaces OutputDirectory only after validation/copying succeeds.
    // Copies every referenced asset, glTF buffer/image, shader and explicit
    // runtime dependency. Missing/unsafe files fail without replacing a package.
    // Synchronous; callers must prevent concurrent scene mutation/export to the same path.
    AutomationResult exportRuntime(const RuntimeExportOptions& options) const;

private:
    AutomationResult requireEntity(Entity entity) const;
    Scene& m_Scene;
};

} // namespace TinyEngine

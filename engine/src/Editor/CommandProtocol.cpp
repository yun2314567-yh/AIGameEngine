#include "TinyEngine/Editor/CommandProtocol.h"

#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Prefab.h"

#include <cmath>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace TinyEngine {
namespace {

bool readEntityId(std::istream& input, Entity& entity) {
    std::uint64_t value = 0;
    if (!(input >> value) || value == 0 || value > std::numeric_limits<std::uint32_t>::max()) return false;
    entity = Entity{static_cast<std::uint32_t>(value)};
    return true;
}

bool readFloat(std::istream& input, float& value) {
    return static_cast<bool>(input >> value) && std::isfinite(value);
}

bool readBool(std::istream& input, bool& value) {
    int raw = -1;
    if (!(input >> raw) || (raw != 0 && raw != 1)) return false;
    value = raw != 0;
    return true;
}

bool hasTrailingInput(std::istream& input) {
    std::string extra;
    return static_cast<bool>(input >> extra);
}

template <typename Component>
bool removeComponent(Scene& scene, Entity entity) {
    return scene.registry().remove<Component>(entity);
}

bool parseBodyType(const std::string& value, BodyType& type) {
    if (value == "static") type = BodyType::Static;
    else if (value == "dynamic") type = BodyType::Dynamic;
    else if (value == "kinematic") type = BodyType::Kinematic;
    else return false;
    return true;
}

bool parseLightType(const std::string& value, LightType& type) {
    if (value == "directional") type = LightType::Directional;
    else if (value == "point") type = LightType::Point;
    else if (value == "spot") type = LightType::Spot;
    else return false;
    return true;
}

void writeError(std::ostream& errors, std::size_t lineNumber, const std::string& message) {
    errors << "ERROR line=" << lineNumber << " message=" << std::quoted(message) << '\n';
}

} // namespace

bool EditorCommandProtocol::run(std::istream& input, std::ostream& output, std::ostream& errors) {
    EditorAutomation automation{m_Scene};
    std::string line;
    std::size_t lineNumber = 0;
    bool succeeded = true;
    while (std::getline(input, line)) {
        ++lineNumber;
        std::istringstream command(line);
        std::string operation;
        if (!(command >> operation) || operation.starts_with('#')) continue;

        std::string result = operation;
        std::string error;
        bool valid = true;
        if (operation == "environment") {
            EnvironmentMap environment;
            if (!(command >> std::quoted(environment.EquirectangularPath) >> environment.Intensity >> environment.RotationRadians
                          >> environment.CubemapResolution >> environment.IrradianceResolution >> environment.PrefilteredMipLevels) || hasTrailingInput(command)) {
                valid = false;
                error = "environment expects a quoted HDR path, intensity, rotation, prefilter resolution, irradiance resolution, and mip count";
            } else valid = m_Scene.SetEnvironment(environment, &error);
        } else if (operation == "render-settings") {
            RenderSettings settings;
            int mapper = 0;
            if (!readBool(command, settings.EnableShadows) || !readBool(command, settings.EnableSsao) ||
                !readBool(command, settings.EnableIbl) || !readBool(command, settings.EnableCompute) ||
                !(command >> settings.ShadowMapResolution >> settings.ShadowPcfSamples >> settings.SsaoRadius >> settings.SsaoBias
                          >> settings.Exposure >> mapper >> settings.SsaoSampleCount >> settings.ShadowDepthBias) || hasTrailingInput(command)) {
                valid = false;
                error = "render-settings expects shadows ssao ibl compute resolution pcf radius ssaoBias exposure mapper samples shadowBias";
            } else {
                settings.Tonemapper = static_cast<ToneMapper>(mapper);
                valid = m_Scene.SetRendering(settings, &error);
            }
        } else if (operation == "create") {
            std::string name;
            if (!(command >> std::quoted(name)) || hasTrailingInput(command)) {
                error = "create expects one quoted entity name";
                valid = false;
            } else {
                const Entity entity = automation.createEntity(std::move(name));
                result += " entity=" + std::to_string(entity.id);
            }
        } else if (operation == "rename") {
            Entity entity;
            std::string name;
            if (!readEntityId(command, entity) || !(command >> std::quoted(name)) || hasTrailingInput(command)) {
                error = "rename expects an entity ID and one quoted name";
                valid = false;
            } else if (!m_Scene.valid(entity)) {
                error = "Entity handle is not valid";
                valid = false;
            } else {
                auto* tag = m_Scene.registry().tryGet<TagComponent>(entity);
                if (!tag) tag = &m_Scene.registry().emplace<TagComponent>(entity);
                tag->Tag = std::move(name);
                result += " entity=" + std::to_string(entity.id);
            }
        } else if (operation == "destroy") {
            Entity entity;
            if (!readEntityId(command, entity) || hasTrailingInput(command)) {
                error = "destroy expects one valid entity ID";
                valid = false;
            } else {
                const auto response = automation.destroyEntity(entity);
                valid = response.Succeeded;
                error = response.Error;
            }
        } else if (operation == "remove") {
            Entity entity;
            std::string componentName;
            if (!readEntityId(command, entity) || !(command >> componentName) || hasTrailingInput(command)) {
                error = "remove expects an entity ID and component name";
                valid = false;
            } else if (!m_Scene.valid(entity)) {
                error = "Entity handle is not valid";
                valid = false;
            } else {
                bool removed = false;
                if (componentName == "tag") removed = removeComponent<TagComponent>(m_Scene, entity);
                else if (componentName == "transform") removed = removeComponent<TransformComponent>(m_Scene, entity);
                else if (componentName == "camera") removed = removeComponent<CameraComponent>(m_Scene, entity);
                else if (componentName == "script") removed = removeComponent<ScriptComponent>(m_Scene, entity);
                else if (componentName == "rigid-body") removed = removeComponent<RigidBodyComponent>(m_Scene, entity);
                else if (componentName == "box-collider") removed = removeComponent<BoxColliderComponent>(m_Scene, entity);
                else if (componentName == "mesh-renderer") removed = removeComponent<MeshRendererComponent>(m_Scene, entity);
                else if (componentName == "light") removed = removeComponent<LightComponent>(m_Scene, entity);
                else if (componentName == "audio-source") removed = removeComponent<AudioSourceComponent>(m_Scene, entity);
                else {
                    error = "unknown component: " + componentName;
                    valid = false;
                }
                if (valid && !removed) {
                    error = "Entity does not have component: " + componentName;
                    valid = false;
                }
            }
        } else if (operation == "transform") {
            Entity entity;
            Vec3 translation;
            Quat rotation;
            Vec3 scale;
            if (!readEntityId(command, entity) || !readFloat(command, translation.x) || !readFloat(command, translation.y) ||
                !readFloat(command, translation.z) || !readFloat(command, rotation.x) || !readFloat(command, rotation.y) ||
                !readFloat(command, rotation.z) || !readFloat(command, rotation.w) || !readFloat(command, scale.x) ||
                !readFloat(command, scale.y) || !readFloat(command, scale.z) || hasTrailingInput(command)) {
                error = "transform expects an entity ID, translation, quaternion, and scale (10 finite numbers)";
                valid = false;
            } else {
                const auto response = automation.setTransform(entity, translation, rotation, scale);
                valid = response.Succeeded;
                error = response.Error;
            }
        } else if (operation == "add") {
            Entity entity;
            std::string componentName;
            if (!readEntityId(command, entity) || !(command >> componentName)) {
                error = "add expects an entity ID and component name";
                valid = false;
            } else if (componentName == "camera") {
                CameraComponent component;
                if (!readFloat(command, component.FieldOfView) || !readFloat(command, component.NearClip) ||
                    !readFloat(command, component.FarClip) || !readBool(command, component.Primary) || hasTrailingInput(command)) {
                    error = "camera expects field-of-view, near clip, far clip, and primary (0 or 1)";
                    valid = false;
                } else {
                    const auto response = automation.addCamera(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "script") {
                std::string module;
                if (!(command >> std::quoted(module)) || hasTrailingInput(command)) {
                    error = "script expects one quoted module name";
                    valid = false;
                } else {
                    const auto response = automation.attachScript(entity, std::move(module));
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "rigid-body") {
                RigidBodyComponent component;
                std::string bodyType;
                if (!(command >> bodyType) || !parseBodyType(bodyType, component.Type) || !readFloat(command, component.Mass) ||
                    !readFloat(command, component.Velocity.x) || !readFloat(command, component.Velocity.y) ||
                    !readFloat(command, component.Velocity.z) || !readBool(command, component.Gravity) || hasTrailingInput(command)) {
                    error = "rigid-body expects type, mass, velocity x/y/z, and gravity (0 or 1)";
                    valid = false;
                } else {
                    const auto response = automation.addRigidBody(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "box-collider") {
                BoxColliderComponent component;
                if (!readFloat(command, component.HalfExtents.x) || !readFloat(command, component.HalfExtents.y) ||
                    !readFloat(command, component.HalfExtents.z) || !readFloat(command, component.Restitution) ||
                    !readFloat(command, component.Friction) || hasTrailingInput(command)) {
                    error = "box-collider expects half-extents x/y/z, restitution, and friction";
                    valid = false;
                } else {
                    const auto response = automation.addBoxCollider(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "mesh-renderer") {
                MeshRendererComponent component;
                if (!(command >> std::quoted(component.Mesh) >> std::quoted(component.Material)) ||
                    !readBool(command, component.Visible) || !readBool(command, component.CastShadows) ||
                    !readBool(command, component.ReceiveShadows) || hasTrailingInput(command)) {
                    error = "mesh-renderer expects quoted mesh/material names and visible/cast/receive flags (0 or 1)";
                    valid = false;
                } else {
                    const auto response = automation.addMeshRenderer(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "light") {
                LightComponent component;
                std::string lightType;
                if (!(command >> lightType) || !parseLightType(lightType, component.Type) ||
                    !readFloat(command, component.Color.x) || !readFloat(command, component.Color.y) ||
                    !readFloat(command, component.Color.z) || !readFloat(command, component.Intensity) ||
                    !readFloat(command, component.Range) || !readFloat(command, component.InnerConeRadians) ||
                    !readFloat(command, component.OuterConeRadians) || !readBool(command, component.CastShadows) || hasTrailingInput(command)) {
                    error = "light expects type, color r/g/b, intensity, range, inner/outer cone, and cast-shadows (0 or 1)";
                    valid = false;
                } else {
                    const auto response = automation.addLight(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else if (componentName == "audio-source") {
                AudioSourceComponent component;
                if (!(command >> std::quoted(component.Clip)) || !readFloat(command, component.Volume) ||
                    !readFloat(command, component.Pitch) || !readBool(command, component.Loop) ||
                    !readBool(command, component.PlayOnStart) || hasTrailingInput(command)) {
                    error = "audio-source expects a quoted clip, volume, pitch, loop, and play-on-start flags (0 or 1)";
                    valid = false;
                } else {
                    const auto response = automation.addAudioSource(entity, component);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            } else {
                error = "unknown component: " + componentName;
                valid = false;
            }
        } else if (operation == "save" || operation == "load") {
            std::string path;
            if (!(command >> std::quoted(path)) || hasTrailingInput(command)) {
                error = operation + " expects one quoted scene path";
                valid = false;
            } else {
                const auto response = operation == "save" ? automation.saveScene(path) : automation.loadScene(path);
                valid = response.Succeeded;
                error = response.Error;
            }
        } else if (operation == "import-gltf") {
            std::string path;
            GltfAsset asset;
            if (!(command >> std::quoted(path)) || hasTrailingInput(command)) {
                error = "import-gltf expects one quoted asset path";
                valid = false;
            } else {
                const auto response = automation.importGltf(path, asset);
                valid = response.Succeeded;
                error = response.Error;
                if (valid) result += " meshes=" + std::to_string(asset.Meshes.size()) + " materials=" + std::to_string(asset.Materials.size());
            }
        } else if (operation == "save-prefab") {
            Entity entity;
            std::string path;
            if (!readEntityId(command, entity) || !(command >> std::quoted(path)) || hasTrailingInput(command)) {
                error = "save-prefab expects an entity ID and one quoted output path";
                valid = false;
            } else {
                Prefab prefab;
                if (!Prefab::fromEntity(m_Scene, entity, prefab, &error)) {
                    valid = false;
                } else if (!prefab.save(path)) {
                    error = "Could not save prefab: " + path;
                    valid = false;
                }
            }
        } else if (operation == "spawn-prefab") {
            std::string path;
            if (!(command >> std::quoted(path)) || hasTrailingInput(command)) {
                error = "spawn-prefab expects one quoted prefab path";
                valid = false;
            } else {
                Prefab prefab;
                if (!Prefab::tryLoad(path, prefab, &error)) {
                    valid = false;
                } else {
                    std::vector<Entity> spawned;
                    if (!prefab.tryInstantiate(m_Scene, spawned, &error)) {
                        valid = false;
                    } else {
                        result += " entities=";
                        for (std::size_t index = 0; index < spawned.size(); ++index) {
                            if (index != 0) result += ',';
                            result += std::to_string(spawned[index].id);
                        }
                    }
                }
            }
        } else if (operation == "export-runtime") {
            RuntimeExportOptions options;
            std::string runtimePath;
            std::string outputPath;
            if (!(command >> std::quoted(runtimePath)) || !(command >> std::quoted(outputPath))) {
                error = "export-runtime expects quoted runtime binary and output directory paths";
                valid = false;
            } else {
                options.RuntimeBinary = runtimePath;
                options.OutputDirectory = outputPath;
                options.AssetRoot = m_AssetRoot;
                std::string filename;
                if (command >> std::quoted(filename)) options.SceneFileName = std::move(filename);
                if (hasTrailingInput(command)) {
                    error = "export-runtime accepts at most one optional quoted scene file name";
                    valid = false;
                } else {
                    const auto response = automation.exportRuntime(options);
                    valid = response.Succeeded;
                    error = response.Error;
                }
            }
        } else if (operation == "inspect") {
            if (hasTrailingInput(command)) {
                error = "inspect takes no arguments";
                valid = false;
            } else {
                output << "SCENE name=" << std::quoted(m_Scene.name()) << " entities=" << m_Scene.registry().size() << '\n';
                for (const Entity entity : m_Scene.registry().entities()) {
                    const auto* tag = m_Scene.registry().tryGet<TagComponent>(entity);
                    output << "ENTITY id=" << entity.id << " name=" << std::quoted(tag ? tag->Tag : std::string("Entity")) << " components=\"";
                    bool needsSeparator = false;
                    const auto appendComponent = [&output, &needsSeparator](bool present, std::string_view name) {
                        if (!present) return;
                        if (needsSeparator) output << ',';
                        output << name;
                        needsSeparator = true;
                    };
                    appendComponent(m_Scene.registry().has<TransformComponent>(entity), "transform");
                    appendComponent(m_Scene.registry().has<CameraComponent>(entity), "camera");
                    appendComponent(m_Scene.registry().has<ScriptComponent>(entity), "script");
                    appendComponent(m_Scene.registry().has<RigidBodyComponent>(entity), "rigid-body");
                    appendComponent(m_Scene.registry().has<BoxColliderComponent>(entity), "box-collider");
                    appendComponent(m_Scene.registry().has<MeshRendererComponent>(entity), "mesh-renderer");
                    appendComponent(m_Scene.registry().has<LightComponent>(entity), "light");
                    appendComponent(m_Scene.registry().has<AudioSourceComponent>(entity), "audio-source");
                    output << "\"\n";
                }
            }
        } else {
            error = "unknown command: " + operation;
            valid = false;
        }

        if (valid) output << "OK " << result << '\n';
        else {
            writeError(errors, lineNumber, error);
            succeeded = false;
        }
    }
    if (input.bad()) {
        writeError(errors, lineNumber + 1, "failed while reading command input");
        return false;
    }
    return succeeded;
}

} // namespace TinyEngine

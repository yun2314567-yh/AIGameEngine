#include "TinyEngine/Scene.h"
#include "TinyEngine/Platform/Paths.h"

#include <fstream>
#include <iomanip>
#include <cmath>
#include <sstream>

namespace TinyEngine {

bool Scene::SetRendering(const RenderSettings& settings, std::string* error) {
    if (!ValidateRenderSettings(settings, error)) return false;
    m_RenderSettings = settings;
    return true;
}

bool Scene::SetEnvironment(const EnvironmentMap& environment, std::string* error) {
    if (!ValidateEnvironmentMap(environment, error)) return false;
    m_Environment = environment;
    return true;
}

Entity Scene::createEntity(std::string name) {
    const Entity entity = m_Registry.create();
    m_Registry.emplace<TagComponent>(entity, std::move(name));
    m_Registry.emplace<TransformComponent>(entity);
    return entity;
}

std::string Scene::serialize() const {
    std::ostringstream output;
    output << std::setprecision(9) << "TINYENGINE_SCENE 2\n" << std::quoted(m_Name) << "\n";
    output << "ENVIRONMENT " << std::quoted(m_Environment.EquirectangularPath) << ' '
           << m_Environment.Intensity << ' ' << m_Environment.RotationRadians << ' '
           << m_Environment.CubemapResolution << ' ' << m_Environment.IrradianceResolution << ' '
           << m_Environment.PrefilteredMipLevels << '\n';
    const auto& settings = m_RenderSettings;
    output << "RENDER " << settings.EnableShadows << ' ' << settings.EnableSsao << ' '
           << settings.EnableIbl << ' ' << settings.EnableCompute << ' '
           << settings.ShadowMapResolution << ' ' << settings.ShadowPcfSamples << ' '
           << settings.SsaoRadius << ' ' << settings.SsaoBias << ' ' << settings.Exposure << ' '
           << static_cast<int>(settings.Tonemapper) << ' ' << settings.SsaoSampleCount << ' '
           << settings.ShadowDepthBias << '\n';
    for (const Entity entity : m_Registry.entities()) {
        const auto* tag = m_Registry.tryGet<TagComponent>(entity);
        const auto* transform = m_Registry.tryGet<TransformComponent>(entity);
        output << "ENTITY " << entity.id << " " << std::quoted(tag ? tag->Tag : "Entity");
        if (transform) {
            output << std::setprecision(9) << " T " << transform->Translation.x << ' ' << transform->Translation.y << ' ' << transform->Translation.z
                    << ' ' << transform->Rotation.x << ' ' << transform->Rotation.y << ' ' << transform->Rotation.z << ' ' << transform->Rotation.w
                    << ' ' << transform->Scale.x << ' ' << transform->Scale.y << ' ' << transform->Scale.z;
        }
        if (const auto* camera = m_Registry.tryGet<CameraComponent>(entity)) {
            output << " C " << camera->FieldOfView << ' ' << camera->NearClip << ' ' << camera->FarClip << ' ' << camera->Primary;
        }
        if (const auto* script = m_Registry.tryGet<ScriptComponent>(entity)) {
            output << " S " << std::quoted(script->Module) << ' ' << script->Enabled;
        }
        if (const auto* rigidBody = m_Registry.tryGet<RigidBodyComponent>(entity)) {
            output << " R " << static_cast<int>(rigidBody->Type) << ' ' << rigidBody->Mass << ' ' << rigidBody->Velocity.x << ' ' << rigidBody->Velocity.y << ' ' << rigidBody->Velocity.z << ' ' << rigidBody->Gravity;
        }
        if (const auto* collider = m_Registry.tryGet<BoxColliderComponent>(entity)) {
            output << " B " << collider->HalfExtents.x << ' ' << collider->HalfExtents.y << ' ' << collider->HalfExtents.z << ' ' << collider->Restitution << ' ' << collider->Friction;
        }
        if (const auto* mesh = m_Registry.tryGet<MeshRendererComponent>(entity)) {
            output << " M " << std::quoted(mesh->Mesh) << ' ' << std::quoted(mesh->Material) << ' ' << mesh->Visible << ' ' << mesh->CastShadows << ' ' << mesh->ReceiveShadows;
        }
        if (const auto* light = m_Registry.tryGet<LightComponent>(entity)) {
            output << " L " << static_cast<int>(light->Type) << ' ' << light->Color.x << ' ' << light->Color.y << ' ' << light->Color.z << ' ' << light->Intensity << ' ' << light->Range << ' ' << light->InnerConeRadians << ' ' << light->OuterConeRadians << ' ' << light->CastShadows;
        }
        if (const auto* audio = m_Registry.tryGet<AudioSourceComponent>(entity)) {
            output << " A " << std::quoted(audio->Clip) << ' ' << audio->Volume << ' ' << audio->Pitch << ' ' << audio->Loop << ' ' << audio->PlayOnStart;
        }
        output << "\n";
    }
    return output.str();
}

Scene Scene::deserialize(std::string_view text) {
    Scene result{std::string{"Untitled"}};
    std::string error;
    if (!tryDeserialize(text, result, error)) return Scene{std::string{"Untitled"}};
    return result;
}

bool Scene::tryDeserialize(std::string_view text, Scene& destination, std::string& error) {
    auto fail = [&error](const std::string& message) {
        error = message;
        return false;
    };
    if (text.size() > 16 * 1024 * 1024) return fail("Scene exceeds the 16 MiB limit");
    std::istringstream input{std::string(text)};
    std::string line;
    if (!std::getline(input, line)) return fail("Scene is empty");
    std::istringstream header{line};
    std::string magic;
    int version = 0;
    std::string trailing;
    if (!(header >> magic >> version) || (header >> trailing) || magic != "TINYENGINE_SCENE" || (version != 1 && version != 2)) {
        return fail("Unsupported or malformed scene header");
    }
    if (!std::getline(input, line)) return fail("Scene name is missing");
    std::istringstream nameRecord{line};
    std::string name;
    if (!(nameRecord >> std::quoted(name)) || (nameRecord >> trailing)) return fail("Malformed scene name");
    Scene scene{name};
    std::size_t lineNumber = 2;
    bool hasEnvironment = false, hasRendering = false, hasEntities = false;
    while (std::getline(input, line)) {
        ++lineNumber;
        if (line.empty()) continue;
        std::istringstream record{line};
        std::string recordType;
        std::uint32_t id = 0;
        std::string tag;
        if (!(record >> recordType)) continue;
        if (version == 2 && recordType == "ENVIRONMENT") {
            EnvironmentMap environment;
            if (hasEnvironment || hasEntities ||
                !(record >> std::quoted(environment.EquirectangularPath) >> environment.Intensity >> environment.RotationRadians
                         >> environment.CubemapResolution >> environment.IrradianceResolution >> environment.PrefilteredMipLevels) ||
                (record >> trailing) || !scene.SetEnvironment(environment, &error))
                return fail("Invalid or duplicate environment at line " + std::to_string(lineNumber));
            hasEnvironment = true;
            continue;
        }
        if (version == 2 && recordType == "RENDER") {
            RenderSettings settings;
            int mapper = 0;
            if (hasRendering || hasEntities ||
                !(record >> settings.EnableShadows >> settings.EnableSsao >> settings.EnableIbl >> settings.EnableCompute
                         >> settings.ShadowMapResolution >> settings.ShadowPcfSamples >> settings.SsaoRadius >> settings.SsaoBias
                         >> settings.Exposure >> mapper >> settings.SsaoSampleCount >> settings.ShadowDepthBias) || (record >> trailing))
                return fail("Invalid or duplicate rendering settings at line " + std::to_string(lineNumber));
            settings.Tonemapper = static_cast<ToneMapper>(mapper);
            if (!scene.SetRendering(settings, &error)) return false;
            hasRendering = true;
            continue;
        }
        hasEntities = true;
        if (scene.m_Registry.size() >= 100000) return fail("Scene exceeds the 100,000 entity limit");
        if (!(record >> id >> std::quoted(tag)) || recordType != "ENTITY") {
            return fail("Malformed entity record at line " + std::to_string(lineNumber));
        }
        if (id == 0 || id == UINT32_MAX || scene.m_Registry.valid(Entity{id})) {
            return fail("Invalid or duplicate entity ID at line " + std::to_string(lineNumber));
        }
        Entity entity{};
        try {
            entity = scene.m_Registry.create(id);
        } catch (const std::exception&) {
            return fail("Could not allocate entity ID at line " + std::to_string(lineNumber));
        }
        scene.m_Registry.emplace<TagComponent>(entity, std::move(tag));
        scene.m_Registry.emplace<TransformComponent>(entity);
        bool hasTransform = false;
        bool hasCamera = false;
        bool hasScript = false;
        bool hasRigidBody = false;
        bool hasCollider = false;
        bool hasMesh = false;
        bool hasLight = false;
        bool hasAudio = false;
        std::string kind;
        while (record >> kind) {
            if (kind == "T") {
                if (hasTransform) return fail("Duplicate transform at line " + std::to_string(lineNumber));
                hasTransform = true;
                auto& transform = scene.m_Registry.get<TransformComponent>(entity);
                if (!(record >> transform.Translation.x >> transform.Translation.y >> transform.Translation.z
                             >> transform.Rotation.x >> transform.Rotation.y >> transform.Rotation.z >> transform.Rotation.w
                             >> transform.Scale.x >> transform.Scale.y >> transform.Scale.z)) {
                    return fail("Malformed transform at line " + std::to_string(lineNumber));
                }
                const float values[] = {transform.Translation.x, transform.Translation.y, transform.Translation.z,
                                        transform.Rotation.x, transform.Rotation.y, transform.Rotation.z, transform.Rotation.w,
                                        transform.Scale.x, transform.Scale.y, transform.Scale.z};
                for (const float value : values) if (!std::isfinite(value)) return fail("Non-finite transform at line " + std::to_string(lineNumber));
                const double lengthSquared = static_cast<double>(transform.Rotation.x) * transform.Rotation.x +
                    static_cast<double>(transform.Rotation.y) * transform.Rotation.y +
                    static_cast<double>(transform.Rotation.z) * transform.Rotation.z +
                    static_cast<double>(transform.Rotation.w) * transform.Rotation.w;
                if (lengthSquared < 1e-12) return fail("Zero quaternion at line " + std::to_string(lineNumber));
            } else if (kind == "C") {
                if (hasCamera) return fail("Duplicate camera at line " + std::to_string(lineNumber));
                hasCamera = true;
                auto& camera = scene.m_Registry.emplace<CameraComponent>(entity);
                if (!(record >> camera.FieldOfView >> camera.NearClip >> camera.FarClip >> camera.Primary)
                    || !std::isfinite(camera.FieldOfView) || !std::isfinite(camera.NearClip) || !std::isfinite(camera.FarClip)
                    || camera.FieldOfView <= 0.0f || camera.FieldOfView >= 180.0f || camera.NearClip <= 0.0f || camera.FarClip <= camera.NearClip) {
                    return fail("Invalid camera at line " + std::to_string(lineNumber));
                }
            } else if (kind == "S") {
                if (hasScript) return fail("Duplicate script at line " + std::to_string(lineNumber));
                hasScript = true;
                auto& script = scene.m_Registry.emplace<ScriptComponent>(entity);
                if (!(record >> std::quoted(script.Module) >> script.Enabled)) return fail("Malformed script at line " + std::to_string(lineNumber));
            } else if (kind == "R") {
                if (hasRigidBody) return fail("Duplicate rigid body at line " + std::to_string(lineNumber));
                hasRigidBody = true;
                auto& rigidBody = scene.m_Registry.emplace<RigidBodyComponent>(entity);
                int type = 0;
                if (!(record >> type >> rigidBody.Mass >> rigidBody.Velocity.x >> rigidBody.Velocity.y >> rigidBody.Velocity.z >> rigidBody.Gravity)
                    || type < 0 || type > 2 || !std::isfinite(rigidBody.Mass) || rigidBody.Mass <= 0.0f
                    || !std::isfinite(rigidBody.Velocity.x) || !std::isfinite(rigidBody.Velocity.y) || !std::isfinite(rigidBody.Velocity.z)) {
                    return fail("Invalid rigid body at line " + std::to_string(lineNumber));
                }
                rigidBody.Type = static_cast<BodyType>(type);
            } else if (kind == "B") {
                if (hasCollider) return fail("Duplicate box collider at line " + std::to_string(lineNumber));
                hasCollider = true;
                auto& collider = scene.m_Registry.emplace<BoxColliderComponent>(entity);
                if (!(record >> collider.HalfExtents.x >> collider.HalfExtents.y >> collider.HalfExtents.z >> collider.Restitution >> collider.Friction)
                    || !std::isfinite(collider.HalfExtents.x) || !std::isfinite(collider.HalfExtents.y) || !std::isfinite(collider.HalfExtents.z)
                    || !std::isfinite(collider.Restitution) || !std::isfinite(collider.Friction)
                    || collider.HalfExtents.x <= 0.0f || collider.HalfExtents.y <= 0.0f || collider.HalfExtents.z <= 0.0f
                    || collider.Restitution < 0.0f || collider.Restitution > 1.0f || collider.Friction < 0.0f) {
                    return fail("Invalid box collider at line " + std::to_string(lineNumber));
                }
            } else if (kind == "M") {
                if (hasMesh) return fail("Duplicate mesh renderer at line " + std::to_string(lineNumber));
                hasMesh = true;
                auto& mesh = scene.m_Registry.emplace<MeshRendererComponent>(entity);
                if (!(record >> std::quoted(mesh.Mesh) >> std::quoted(mesh.Material) >> mesh.Visible >> mesh.CastShadows >> mesh.ReceiveShadows)) return fail("Malformed mesh renderer at line " + std::to_string(lineNumber));
            } else if (kind == "L") {
                if (hasLight) return fail("Duplicate light at line " + std::to_string(lineNumber));
                hasLight = true;
                auto& light = scene.m_Registry.emplace<LightComponent>(entity);
                int type = 0;
                if (!(record >> type >> light.Color.x >> light.Color.y >> light.Color.z >> light.Intensity >> light.Range >> light.InnerConeRadians >> light.OuterConeRadians >> light.CastShadows)
                    || type < 0 || type > 2 || !std::isfinite(light.Color.x) || !std::isfinite(light.Color.y) || !std::isfinite(light.Color.z)
                    || !std::isfinite(light.Intensity) || !std::isfinite(light.Range) || !std::isfinite(light.InnerConeRadians) || !std::isfinite(light.OuterConeRadians)
                    || light.Intensity < 0.0f || light.Range <= 0.0f || light.InnerConeRadians < 0.0f || light.OuterConeRadians < light.InnerConeRadians) {
                    return fail("Invalid light at line " + std::to_string(lineNumber));
                }
                light.Type = static_cast<LightType>(type);
                if (!ValidateLight(light)) return fail("Invalid light at line " + std::to_string(lineNumber));
            } else if (kind == "A") {
                if (hasAudio) return fail("Duplicate audio source at line " + std::to_string(lineNumber));
                hasAudio = true;
                auto& audio = scene.m_Registry.emplace<AudioSourceComponent>(entity);
                if (!(record >> std::quoted(audio.Clip) >> audio.Volume >> audio.Pitch >> audio.Loop >> audio.PlayOnStart)
                    || !std::isfinite(audio.Volume) || !std::isfinite(audio.Pitch) || audio.Volume < 0.0f || audio.Pitch <= 0.0f) {
                    return fail("Invalid audio source at line " + std::to_string(lineNumber));
                }
            } else {
                return fail("Unknown component '" + kind + "' at line " + std::to_string(lineNumber));
            }
        }
        if (!record.eof()) return fail("Malformed component record at line " + std::to_string(lineNumber));
    }
    destination = std::move(scene);
    error.clear();
    return true;
}

bool Scene::save(const std::string& path) const {
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << serialize();
    return static_cast<bool>(file);
}

Scene Scene::load(const std::string& path) {
    std::string text;
    if (!ReadTextFile(path, 16 * 1024 * 1024, text)) return {};
    return deserialize(text);
}

} // namespace TinyEngine

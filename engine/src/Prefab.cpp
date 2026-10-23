#include "TinyEngine/Prefab.h"
#include "TinyEngine/Platform/Paths.h"

#include <fstream>
#include <iterator>
#include <utility>

namespace TinyEngine {

namespace {

template <typename Component>
void copyComponent(const Scene& source, Entity sourceEntity, Scene& destination, Entity targetEntity) {
    if (const auto* component = source.registry().tryGet<Component>(sourceEntity)) {
        if (auto* existing = destination.registry().tryGet<Component>(targetEntity)) {
            *existing = *component;
        } else {
            destination.registry().emplace<Component>(targetEntity) = *component;
        }
    }
}

void copyComponents(const Scene& source, Entity sourceEntity, Scene& destination, Entity targetEntity) {
    copyComponent<TransformComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<CameraComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<ScriptComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<RigidBodyComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<BoxColliderComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<MeshRendererComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<LightComponent>(source, sourceEntity, destination, targetEntity);
    copyComponent<AudioSourceComponent>(source, sourceEntity, destination, targetEntity);
}

} // namespace

bool Prefab::fromEntity(const Scene& scene, Entity entity, Prefab& prefab, std::string* error) {
    if (!scene.valid(entity)) {
        if (error) *error = "Cannot create a prefab from an invalid entity";
        return false;
    }
    const auto* tag = scene.registry().tryGet<TagComponent>(entity);
    Scene source{scene.name()};
    const Entity prefabEntity = source.createEntity(tag ? tag->Tag : "Entity");
    copyComponents(scene, entity, source, prefabEntity);
    prefab = fromScene(source);
    if (error) error->clear();
    return true;
}

std::vector<Entity> Prefab::instantiate(Scene& destination) const {
    std::vector<Entity> entities;
    tryInstantiate(destination, entities);
    return entities;
}

bool Prefab::tryInstantiate(Scene& destination, std::vector<Entity>& entities, std::string* error) const {
    Scene source;
    std::string parseError;
    if (!Scene::tryDeserialize(m_SerializedScene, source, parseError)) {
        if (error) *error = std::move(parseError);
        return false;
    }
    std::vector<Entity> created;
    for (const Entity sourceEntity : source.registry().entities()) {
        const auto* tag = source.registry().tryGet<TagComponent>(sourceEntity);
        const Entity targetEntity = destination.createEntity(tag ? tag->Tag : "Entity");
        copyComponents(source, sourceEntity, destination, targetEntity);
        created.push_back(targetEntity);
    }
    entities = std::move(created);
    if (error) error->clear();
    return true;
}

bool Prefab::save(const std::string& path) const {
    Scene validated;
    std::string error;
    if (!Scene::tryDeserialize(m_SerializedScene, validated, error)) return false;
    std::ofstream file(path, std::ios::binary);
    if (!file) return false;
    file << m_SerializedScene;
    return static_cast<bool>(file);
}

Prefab Prefab::load(const std::string& path) {
    Prefab prefab;
    tryLoad(path, prefab);
    return prefab;
}

bool Prefab::tryLoad(const std::string& path, Prefab& prefab, std::string* error) {
    std::string serialized;
    if (!ReadTextFile(path, 16 * 1024 * 1024, serialized, error)) return false;
    Scene validated;
    std::string parseError;
    if (!Scene::tryDeserialize(serialized, validated, parseError)) {
        if (error) *error = std::move(parseError);
        return false;
    }
    prefab = Prefab{std::move(serialized)};
    if (error) error->clear();
    return true;
}

} // namespace TinyEngine

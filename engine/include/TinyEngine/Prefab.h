#pragma once

#include "TinyEngine/Scene.h"

#include <string>
#include <vector>

namespace TinyEngine {

class Prefab {
public:
    Prefab() = default;
    explicit Prefab(std::string serializedScene) : m_SerializedScene(std::move(serializedScene)) {}

    static Prefab fromScene(const Scene& scene) { return Prefab(scene.serialize()); }
    static bool fromEntity(const Scene& scene, Entity entity, Prefab& prefab, std::string* error = nullptr);
    [[nodiscard]] const std::string& serializedScene() const { return m_SerializedScene; }
    [[nodiscard]] std::vector<Entity> instantiate(Scene& destination) const;
    bool tryInstantiate(Scene& destination, std::vector<Entity>& entities, std::string* error = nullptr) const;
    bool save(const std::string& path) const;
    static Prefab load(const std::string& path);
    static bool tryLoad(const std::string& path, Prefab& prefab, std::string* error = nullptr);

private:
    std::string m_SerializedScene;
};

} // namespace TinyEngine

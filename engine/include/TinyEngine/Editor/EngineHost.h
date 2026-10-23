#pragma once

#include "TinyEngine/Renderer/Renderer.h"
#include "TinyEngine/Scene.h"

#include <functional>
#include <string>

namespace TinyEngine {

enum class EngineState { Stopped, Running, Faulted };

class EngineHost {
public:
    using UpdateCallback = std::function<void(Scene&, float)>;

    EngineHost(Scene& scene, Renderer& renderer) : m_Scene(scene), m_Renderer(renderer) {}
    bool start();
    bool tick(float deltaSeconds, std::uint32_t width, std::uint32_t height, std::string* error = nullptr);
    void stop();
    void setUpdateCallback(UpdateCallback callback) { m_Update = std::move(callback); }
    [[nodiscard]] EngineState state() const { return m_State; }
    [[nodiscard]] std::uint64_t frameIndex() const { return m_FrameIndex; }
    [[nodiscard]] Scene& scene() { return m_Scene; }

private:
    Scene& m_Scene;
    Renderer& m_Renderer;
    UpdateCallback m_Update;
    EngineState m_State = EngineState::Stopped;
    std::uint64_t m_FrameIndex = 0;
};

class EditorSession {
public:
    explicit EditorSession(Scene& scene) : m_Scene(scene) {}
    void select(Entity entity);
    [[nodiscard]] Entity selection() const { return m_Selection; }
    bool applyGizmoTransform(const Vec3& translation, const Quat& rotation, const Vec3& scale);
    bool enterPlayMode();
    void leavePlayMode();
    [[nodiscard]] bool inPlayMode() const { return m_Playing; }

private:
    Scene& m_Scene;
    Entity m_Selection{};
    bool m_Playing = false;
};

} // namespace TinyEngine

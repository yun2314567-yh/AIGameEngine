#include "TinyEngine/Editor/EngineHost.h"

#include <cmath>

namespace TinyEngine {

bool EngineHost::start() {
    if (m_State == EngineState::Faulted) return false;
    m_State = EngineState::Running;
    m_FrameIndex = 0;
    return true;
}

bool EngineHost::tick(float deltaSeconds, std::uint32_t width, std::uint32_t height, std::string* error) {
    if (m_State != EngineState::Running) {
        if (error) *error = "EngineHost must be running before tick";
        return false;
    }
    if (!std::isfinite(deltaSeconds) || deltaSeconds < 0.0f) {
        if (error) *error = "Frame delta must be finite and non-negative";
        return false;
    }
    if (m_Update) m_Update(m_Scene, deltaSeconds);
    const bool rendered = m_Renderer.render({width, height, deltaSeconds, m_FrameIndex}, error);
    if (!rendered) {
        m_State = EngineState::Faulted;
        return false;
    }
    ++m_FrameIndex;
    return true;
}

void EngineHost::stop() { if (m_State != EngineState::Faulted) m_State = EngineState::Stopped; }

void EditorSession::select(Entity entity) { m_Selection = m_Scene.valid(entity) ? entity : Entity{}; }

bool EditorSession::applyGizmoTransform(const Vec3& translation, const Quat& rotation, const Vec3& scale) {
    if (m_Playing || !m_Scene.valid(m_Selection)) return false;
    auto* transform = m_Scene.registry().tryGet<TransformComponent>(m_Selection);
    if (!transform) return false;
    transform->Translation = translation;
    transform->Rotation = rotation.normalized();
    transform->Scale = scale;
    return true;
}

bool EditorSession::enterPlayMode() {
    if (m_Playing) return false;
    m_Playing = true;
    return true;
}

void EditorSession::leavePlayMode() { m_Playing = false; }

} // namespace TinyEngine

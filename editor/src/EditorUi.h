#pragma once

#include "TinyEngine/Core.h"
#include "TinyEngine/Editor/Automation.h"

#include <array>
#include <filesystem>
#include <string>
#include <utility>

namespace TinyEngine {

struct EditorUiOptions {
    std::filesystem::path ScenePath;
    std::filesystem::path AssetRoot = ".";
    std::filesystem::path RuntimeBinary;
};

struct EditorViewport {
    float X = 0.0f;
    float Y = 0.0f;
    float Width = 1.0f;
    float Height = 1.0f;
};

// Draws into an existing ImGui frame. The application owns the window,
// graphics backend, ImGui context, and submission of the resulting draw data.
class EditorUi {
public:
    EditorUi(Engine& engine, const EditorUiOptions& options);
    void draw(float deltaSeconds);
    bool tick(float deltaSeconds);
    bool startPlay();
    bool stopPlay();
    void ReportError(std::string message) { setStatus(std::move(message), true); }

    [[nodiscard]] const Mat4& cameraView() const { return m_View; }
    [[nodiscard]] const Mat4& cameraProjection() const { return m_Projection; }
    [[nodiscard]] const Vec3& cameraPosition() const { return m_CameraPosition; }
    [[nodiscard]] std::filesystem::path assetRoot() const { return std::filesystem::path{m_AssetRoot.data()}; }
    [[nodiscard]] const EditorViewport& viewport() const { return m_Viewport; }
    [[nodiscard]] bool closeRequested() const { return m_CloseRequested; }
    [[nodiscard]] bool inPlayMode() const { return m_Session.inPlayMode(); }

private:
    void drawWorkspace();
    void drawHierarchy();
    void drawInspector();
    void drawAssets();
    void DrawRendering();
    void drawViewport(float deltaSeconds);
    void saveScene();
    void loadScene();
    void importAsset();
    void exportRuntime();
    void setStatus(std::string message, bool failed = false);
    void updateCamera(float aspectRatio);

    Engine& m_Engine;
    EditorSession m_Session;
    EditorAutomation m_Automation;
    std::array<char, 1024> m_ScenePath{};
    std::array<char, 1024> m_AssetRoot{};
    std::array<char, 1024> m_ImportPath{};
    std::array<char, 1024> m_RuntimeBinary{};
    std::array<char, 1024> m_ExportDirectory{};
    std::string m_PlaySnapshot;
    std::string m_Status;
    bool m_StatusFailed = false;
    bool m_CloseRequested = false;
    bool m_LocalGizmo = true;
    int m_GizmoOperation = 0;
    Vec3 m_CameraPosition{5.0f, 3.0f, 7.0f};
    float m_CameraYaw = -2.191f;
    float m_CameraPitch = -0.335f;
    Mat4 m_View = Mat4::identity();
    Mat4 m_Projection = Mat4::identity();
    EditorViewport m_Viewport;
};

} // namespace TinyEngine

#include "EditorUi.h"
#include "TinyEngine/Renderer/GraphicsContext.h"

#include <imgui.h>
#include <ImGuizmo.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <system_error>
#include <utility>
#include <vector>

namespace TinyEngine {
namespace {
constexpr float Pi = 3.14159265358979323846f;
constexpr float DegreesToRadians = Pi / 180.0f;

template <std::size_t Size>
void copyText(std::array<char, Size>& destination, const std::string& source) {
    std::snprintf(destination.data(), destination.size(), "%s", source.c_str());
}

bool editText(const char* label, std::string& text) {
    std::array<char, 1024> buffer{};
    copyText(buffer, text);
    if (!ImGui::InputText(label, buffer.data(), buffer.size())) return false;
    text = buffer.data();
    return true;
}

bool editVector(const char* label, Vec3& value, float speed = 0.05f, float minimum = 0.0f,
                float maximum = 0.0f) {
    float values[]{value.x, value.y, value.z};
    if (!ImGui::DragFloat3(label, values, speed, minimum, maximum, "%.3f")) return false;
    value = {values[0], values[1], values[2]};
    return true;
}

bool relativeAssetPath(const std::filesystem::path& root, const char* path,
                       std::filesystem::path& source, std::string& error) {
    const std::filesystem::path relative{path};
    if (relative.empty() || relative.has_root_path()) {
        error = "Choose an asset path relative to the asset root";
        return false;
    }
    for (const auto& part : relative) {
        if (part == "..") {
            error = "Asset paths must stay inside the asset root";
            return false;
        }
    }
    std::error_code filesystemError;
    const auto absoluteRoot = std::filesystem::weakly_canonical(root, filesystemError);
    if (filesystemError) {
        error = "Could not resolve asset root: " + filesystemError.message();
        return false;
    }
    source = std::filesystem::weakly_canonical(absoluteRoot / relative, filesystemError);
    const auto contained = source.lexically_relative(absoluteRoot);
    if (filesystemError || contained.empty()) {
        error = "Could not resolve the asset path";
        return false;
    }
    for (const auto& part : contained) {
        if (part == "..") {
            error = "Asset paths must stay inside the asset root";
            return false;
        }
    }
    return true;
}
} // namespace

EditorUi::EditorUi(Engine& engine, const EditorUiOptions& options)
    : m_Engine(engine), m_Session(engine.scene()), m_Automation(engine.scene()) {
    copyText(m_ScenePath, options.ScenePath.empty() ? "Untitled.tiny" : options.ScenePath.string());
    copyText(m_AssetRoot, options.AssetRoot.string());
    copyText(m_RuntimeBinary, options.RuntimeBinary.string());
    copyText(m_ImportPath, "meshes/model.gltf");
    copyText(m_ExportDirectory, "dist/game");
    updateCamera(16.0f / 9.0f);
}

void EditorUi::setStatus(std::string message, bool failed) {
    m_Status = std::move(message);
    m_StatusFailed = failed;
}

bool EditorUi::startPlay() {
    if (m_Session.inPlayMode()) return false;
    std::string error;
    if (!m_Engine.loadScriptModules(m_AssetRoot.data(), &error)) {
        setStatus(std::move(error), true);
        return false;
    }
    m_PlaySnapshot = m_Engine.scene().serialize();
    if (AudioSystem::IsAvailable() && !m_Engine.StartAudio(m_AssetRoot.data(), {}, &error)) {
        setStatus("Could not start audio: " + error, true);
        return false;
    }
    m_Engine.physics().clear();
    if (!m_Session.enterPlayMode()) { m_Engine.StopAudio(); return false; }
    setStatus("Play mode started");
    return true;
}

bool EditorUi::stopPlay() {
    if (!m_Session.inPlayMode()) return false;
    Scene authoringScene;
    std::string error;
    if (!Scene::tryDeserialize(m_PlaySnapshot, authoringScene, error)) {
        setStatus("Could not restore the edited scene: " + error, true);
        return false;
    }
    const Entity selection = m_Session.selection();
    m_Engine.StopAudio();
    m_Engine.scene() = std::move(authoringScene);
    m_Engine.physics().clear();
    m_Session.leavePlayMode();
    m_Session.select(selection);
    m_PlaySnapshot.clear();
    setStatus("Play mode stopped; edited scene restored");
    return true;
}

bool EditorUi::tick(float deltaSeconds) {
    if (!m_Session.inPlayMode()) return true;
    m_Engine.update(deltaSeconds);
    if (!m_Engine.AudioError().empty()) {
        const auto error = m_Engine.AudioError();
        stopPlay();
        setStatus("Audio update failed: " + error, true);
        return false;
    }
    const auto* scripts = m_Engine.luaScripts();
    if (scripts && !scripts->lastError().empty()) {
        const std::string error = scripts->lastError();
        stopPlay();
        setStatus("Script update failed: " + error, true);
        return false;
    }
    return true;
}

void EditorUi::saveScene() {
    const auto result = m_Automation.saveScene(m_ScenePath.data());
    setStatus(result ? "Scene saved" : result.Error, !result);
}

void EditorUi::loadScene() {
    if (m_Session.inPlayMode()) return;
    const auto result = m_Automation.loadScene(m_ScenePath.data());
    if (result) {
        m_Session.select({});
        m_Engine.physics().clear();
    }
    setStatus(result ? "Scene loaded" : result.Error, !result);
}

void EditorUi::importAsset() {
    if (m_Session.inPlayMode()) return;
    std::filesystem::path source;
    std::string error;
    if (!relativeAssetPath(m_AssetRoot.data(), m_ImportPath.data(), source, error)) {
        setStatus(std::move(error), true);
        return;
    }
    GltfAsset asset;
    const auto result = m_Automation.importGltf(source, asset, {true});
    if (!result) {
        setStatus(result.Error, true);
        return;
    }
    if (asset.Meshes.empty()) {
        setStatus("The imported glTF file has no meshes", true);
        return;
    }
    const std::string meshPath = std::filesystem::path{m_ImportPath.data()}.generic_string();
    Entity firstEntity{};
    std::vector<Entity> createdEntities;
    for (std::size_t index = 0; index < asset.Meshes.size(); ++index) {
        const auto& importedMesh = asset.Meshes[index];
        const std::string entityName = importedMesh.Name.empty()
            ? source.stem().string() + " " + std::to_string(index)
            : importedMesh.Name;
        const Entity entity = m_Automation.createEntity(entityName);
        MeshRendererComponent mesh;
        mesh.Mesh = meshPath + "#" + std::to_string(index);
        const auto addResult = m_Automation.addMeshRenderer(entity, mesh);
        if (!addResult) {
            m_Automation.destroyEntity(entity);
            for (const Entity created : createdEntities) m_Automation.destroyEntity(created);
            setStatus(addResult.Error, true);
            return;
        }
        if (!firstEntity) firstEntity = entity;
        createdEntities.push_back(entity);
    }
    m_Session.select(firstEntity);
    setStatus("Imported " + std::to_string(asset.Meshes.size()) + " mesh(es), " +
              std::to_string(asset.Materials.size()) + " material(s)");
}

void EditorUi::exportRuntime() {
    RuntimeExportOptions options;
    options.RuntimeBinary = m_RuntimeBinary.data();
    options.OutputDirectory = m_ExportDirectory.data();
    options.AssetRoot = m_AssetRoot.data();
    const auto result = m_Automation.exportRuntime(options);
    setStatus(result ? "Runtime package exported" : result.Error, !result);
}

void EditorUi::drawWorkspace() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::SetNextWindowViewport(viewport->ID);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    constexpr ImGuiWindowFlags flags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking |
        ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus |
        ImGuiWindowFlags_NoBackground;
    ImGui::Begin("TinyEngine Workspace", nullptr, flags);
    ImGui::PopStyleVar(2);
    if (ImGui::BeginMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            ImGui::BeginDisabled(m_Session.inPlayMode());
            if (ImGui::MenuItem("New Scene")) {
                m_Engine.scene() = Scene{"Untitled"};
                m_Engine.physics().clear();
                m_Session.select({});
                setStatus("New scene created");
            }
            if (ImGui::MenuItem("Save Scene", "Ctrl+S")) saveScene();
            if (ImGui::MenuItem("Load Scene")) loadScene();
            if (ImGui::MenuItem("Export Runtime")) exportRuntime();
            ImGui::EndDisabled();
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) m_CloseRequested = true;
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu("Entity")) {
            ImGui::BeginDisabled(m_Session.inPlayMode());
            if (ImGui::MenuItem("Create Empty")) m_Session.select(m_Automation.createEntity());
            if (ImGui::MenuItem("Delete Selected", nullptr, false, m_Engine.scene().valid(m_Session.selection()))) {
                m_Automation.destroyEntity(m_Session.selection());
                m_Session.select({});
            }
            ImGui::EndDisabled();
            ImGui::EndMenu();
        }
        ImGui::EndMenuBar();
    }
    if (m_Session.inPlayMode()) {
        if (ImGui::Button("Stop")) stopPlay();
    } else if (ImGui::Button("Play")) startPlay();
    ImGui::SameLine();
    ImGui::TextUnformatted(m_Engine.scene().name().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%zu entities", m_Engine.scene().registry().size());
    if (!m_Status.empty()) {
        ImGui::SameLine();
        if (m_StatusFailed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4{1.0f, 0.35f, 0.3f, 1.0f});
        ImGui::TextUnformatted(m_Status.c_str());
        if (m_StatusFailed) ImGui::PopStyleColor();
    }
    ImGui::DockSpace(ImGui::GetID("TinyEngineDockspace"), ImVec2{0.0f, 0.0f}, ImGuiDockNodeFlags_PassthruCentralNode);
    ImGui::End();
}

void EditorUi::drawHierarchy() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y + 75.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({240.0f, viewport->WorkSize.y - 310.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Hierarchy");
    ImGui::BeginDisabled(m_Session.inPlayMode());
    if (ImGui::Button("+ Entity")) m_Session.select(m_Automation.createEntity());
    ImGui::EndDisabled();
    ImGui::Separator();
    const auto entities = m_Engine.scene().registry().entities();
    for (const Entity entity : entities) {
        const auto* tag = m_Engine.scene().registry().tryGet<TagComponent>(entity);
        const std::string label = (tag ? tag->Tag : "Entity") + "##" + std::to_string(entity.id);
        if (ImGui::Selectable(label.c_str(), m_Session.selection() == entity)) m_Session.select(entity);
    }
    ImGui::End();
}

void EditorUi::drawInspector() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + viewport->WorkSize.x - 310.0f, viewport->WorkPos.y + 75.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({310.0f, viewport->WorkSize.y - 310.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");
    const Entity entity = m_Session.selection();
    auto& registry = m_Engine.scene().registry();
    if (!registry.valid(entity)) {
        ImGui::TextDisabled("No selection");
        ImGui::End();
        return;
    }
    ImGui::TextDisabled("Entity %u", entity.id);
    ImGui::BeginDisabled(m_Session.inPlayMode());
    if (auto* tag = registry.tryGet<TagComponent>(entity)) editText("Name", tag->Tag);
    if (auto* transform = registry.tryGet<TransformComponent>(entity); transform && ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        editVector("Translation", transform->Translation);
        float quaternion[]{transform->Rotation.x, transform->Rotation.y, transform->Rotation.z, transform->Rotation.w};
        if (ImGui::DragFloat4("Rotation", quaternion, 0.01f, -1.0f, 1.0f, "%.3f"))
            transform->Rotation = Quat{quaternion[0], quaternion[1], quaternion[2], quaternion[3]}.normalized();
        editVector("Scale", transform->Scale);
    }
    if (auto* camera = registry.tryGet<CameraComponent>(entity); camera && ImGui::CollapsingHeader("Camera", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat("Field of view", &camera->FieldOfView, 0.25f, 1.0f, 179.0f);
        ImGui::DragFloat("Near clip", &camera->NearClip, 0.01f, 0.001f, camera->FarClip - 0.001f);
        ImGui::DragFloat("Far clip", &camera->FarClip, 1.0f, camera->NearClip + 0.001f, 100000.0f);
        ImGui::Checkbox("Primary", &camera->Primary);
        if (ImGui::Button("Remove Camera")) registry.remove<CameraComponent>(entity);
    }
    if (auto* mesh = registry.tryGet<MeshRendererComponent>(entity); mesh && ImGui::CollapsingHeader("Mesh Renderer", ImGuiTreeNodeFlags_DefaultOpen)) {
        editText("Mesh", mesh->Mesh); editText("Material", mesh->Material);
        ImGui::Checkbox("Visible", &mesh->Visible); ImGui::Checkbox("Cast shadows##Mesh", &mesh->CastShadows);
        ImGui::Checkbox("Receive shadows", &mesh->ReceiveShadows);
        if (ImGui::Button("Remove Mesh Renderer")) registry.remove<MeshRendererComponent>(entity);
    }
    if (auto* light = registry.tryGet<LightComponent>(entity); light && ImGui::CollapsingHeader("Light", ImGuiTreeNodeFlags_DefaultOpen)) {
        int type = static_cast<int>(light->Type);
        if (ImGui::Combo("Light type", &type, "Directional\0Point\0Spot\0")) light->Type = static_cast<LightType>(type);
        float color[]{light->Color.x, light->Color.y, light->Color.z};
        if (ImGui::ColorEdit3("Color", color)) light->Color = {color[0], color[1], color[2]};
        ImGui::DragFloat("Intensity", &light->Intensity, 0.1f, 0.0f, 100000.0f);
        ImGui::DragFloat("Range", &light->Range, 0.1f, 0.001f, 100000.0f);
        ImGui::DragFloat("Inner cone", &light->InnerConeRadians, 0.01f, 0.0f, light->OuterConeRadians);
        ImGui::DragFloat("Outer cone", &light->OuterConeRadians, 0.01f, light->InnerConeRadians, Pi);
        ImGui::Checkbox("Cast shadows##Light", &light->CastShadows);
        if (ImGui::Button("Remove Light")) registry.remove<LightComponent>(entity);
    }
    if (auto* body = registry.tryGet<RigidBodyComponent>(entity); body && ImGui::CollapsingHeader("Rigid Body", ImGuiTreeNodeFlags_DefaultOpen)) {
        int type = static_cast<int>(body->Type);
        if (ImGui::Combo("Body type", &type, "Static\0Dynamic\0Kinematic\0")) body->Type = static_cast<BodyType>(type);
        ImGui::DragFloat("Mass", &body->Mass, 0.05f, 0.001f, 1000000.0f);
        editVector("Velocity", body->Velocity);
        ImGui::Checkbox("Gravity", &body->Gravity);
        if (ImGui::Button("Remove Rigid Body")) registry.remove<RigidBodyComponent>(entity);
    }
    if (auto* collider = registry.tryGet<BoxColliderComponent>(entity); collider && ImGui::CollapsingHeader("Box Collider", ImGuiTreeNodeFlags_DefaultOpen)) {
        editVector("Half extents", collider->HalfExtents, 0.05f, 0.001f, 100000.0f);
        ImGui::SliderFloat("Restitution", &collider->Restitution, 0.0f, 1.0f);
        ImGui::SliderFloat("Friction", &collider->Friction, 0.0f, 1.0f);
        if (ImGui::Button("Remove Box Collider")) registry.remove<BoxColliderComponent>(entity);
    }
    if (auto* audio = registry.tryGet<AudioSourceComponent>(entity); audio && ImGui::CollapsingHeader("Audio Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        editText("Clip", audio->Clip);
        ImGui::SliderFloat("Volume", &audio->Volume, 0.0f, 1.0f);
        ImGui::DragFloat("Pitch", &audio->Pitch, 0.01f, 0.01f, 4.0f);
        ImGui::Checkbox("Loop", &audio->Loop); ImGui::Checkbox("Play on start", &audio->PlayOnStart);
        if (ImGui::Button("Remove Audio Source")) registry.remove<AudioSourceComponent>(entity);
    }
    if (auto* script = registry.tryGet<ScriptComponent>(entity); script && ImGui::CollapsingHeader("Script", ImGuiTreeNodeFlags_DefaultOpen)) {
        editText("Module", script->Module); ImGui::Checkbox("Enabled", &script->Enabled);
        if (ImGui::Button("Remove Script")) registry.remove<ScriptComponent>(entity);
    }
    if (ImGui::Button("+ Component")) ImGui::OpenPopup("AddComponent");
    if (ImGui::BeginPopup("AddComponent")) {
        if (!registry.has<CameraComponent>(entity) && ImGui::MenuItem("Camera")) m_Automation.addCamera(entity);
        if (!registry.has<MeshRendererComponent>(entity) && ImGui::MenuItem("Mesh Renderer")) m_Automation.addMeshRenderer(entity);
        if (!registry.has<LightComponent>(entity) && ImGui::MenuItem("Light")) m_Automation.addLight(entity);
        if (!registry.has<RigidBodyComponent>(entity) && ImGui::MenuItem("Rigid Body")) m_Automation.addRigidBody(entity);
        if (!registry.has<BoxColliderComponent>(entity) && ImGui::MenuItem("Box Collider")) m_Automation.addBoxCollider(entity);
        if (!registry.has<AudioSourceComponent>(entity) && ImGui::MenuItem("Audio Source")) m_Automation.addAudioSource(entity);
        if (!registry.has<ScriptComponent>(entity) && ImGui::MenuItem("Script")) m_Automation.attachScript(entity, "scripts/game.lua");
        ImGui::EndPopup();
    }
    if (ImGui::Button("Delete Entity")) {
        m_Automation.destroyEntity(entity);
        m_Session.select({});
    }
    ImGui::EndDisabled();
    ImGui::End();
}

void EditorUi::DrawRendering() {
    ImGui::Begin("Rendering");
    ImGui::BeginDisabled(m_Session.inPlayMode());
    auto settings = m_Engine.scene().Rendering();
    bool changed = ImGui::Checkbox("Shadows", &settings.EnableShadows);
    changed |= ImGui::Checkbox("SSAO", &settings.EnableSsao);
    changed |= ImGui::Checkbox("Image based lighting", &settings.EnableIbl);
    changed |= ImGui::DragFloat("Exposure", &settings.Exposure, 0.01f, 0.0f, 20.0f);
    int mapper = static_cast<int>(settings.Tonemapper);
    changed |= ImGui::Combo("Tone mapper", &mapper, "ACES\0Reinhard\0Linear\0");
    settings.Tonemapper = static_cast<ToneMapper>(mapper);
    changed |= ImGui::DragFloat("SSAO radius", &settings.SsaoRadius, 0.01f, 0.01f, 10.0f);
    changed |= ImGui::DragFloat("SSAO bias", &settings.SsaoBias, 0.001f, 0.0f, 1.0f);
    int samples = static_cast<int>(settings.SsaoSampleCount);
    changed |= ImGui::SliderInt("SSAO samples", &samples, 1, 64);
    settings.SsaoSampleCount = static_cast<std::uint32_t>(samples);
    int pcf = static_cast<int>(settings.ShadowPcfSamples);
    changed |= ImGui::SliderInt("PCF samples", &pcf, 1, 16);
    settings.ShadowPcfSamples = static_cast<std::uint32_t>(pcf);
    changed |= ImGui::DragFloat("Shadow bias", &settings.ShadowDepthBias, 0.0001f, 0.0f, 0.05f, "%.4f");
    int resolution = static_cast<int>(settings.ShadowMapResolution);
    changed |= ImGui::InputInt("Shadow resolution", &resolution, 256, 1024, ImGuiInputTextFlags_EnterReturnsTrue);
    settings.ShadowMapResolution = static_cast<std::uint32_t>(resolution);
    std::string error;
    if (changed && !m_Engine.scene().SetRendering(settings, &error)) setStatus(error, true);

    ImGui::SeparatorText("HDR environment (Enter to apply)");
    auto environment = m_Engine.scene().Environment();
    std::array<char, 1024> path{};
    copyText(path, environment.EquirectangularPath);
    bool environmentChanged = ImGui::InputText("HDR asset", path.data(), path.size(), ImGuiInputTextFlags_EnterReturnsTrue);
    environment.EquirectangularPath = path.data();
    environmentChanged |= ImGui::InputFloat("Intensity", &environment.Intensity, 0.1f, 1.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    environmentChanged |= ImGui::InputFloat("Rotation (radians)", &environment.RotationRadians, 0.1f, 1.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue);
    int prefilterSize = static_cast<int>(environment.CubemapResolution);
    environmentChanged |= ImGui::InputInt("Prefilter resolution", &prefilterSize, 16, 64, ImGuiInputTextFlags_EnterReturnsTrue);
    environment.CubemapResolution = static_cast<std::uint32_t>(prefilterSize);
    int irradianceSize = static_cast<int>(environment.IrradianceResolution);
    environmentChanged |= ImGui::InputInt("Irradiance resolution", &irradianceSize, 8, 16, ImGuiInputTextFlags_EnterReturnsTrue);
    environment.IrradianceResolution = static_cast<std::uint32_t>(irradianceSize);
    int mipLevels = static_cast<int>(environment.PrefilteredMipLevels);
    environmentChanged |= ImGui::InputInt("Prefilter mip levels", &mipLevels, 1, 1, ImGuiInputTextFlags_EnterReturnsTrue);
    environment.PrefilteredMipLevels = static_cast<std::uint32_t>(mipLevels);
    if (environmentChanged && !m_Engine.scene().SetEnvironment(environment, &error)) setStatus(error, true);
    ImGui::EndDisabled();
    ImGui::End();
}

void EditorUi::drawAssets() {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x, viewport->WorkPos.y + viewport->WorkSize.y - 225.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({viewport->WorkSize.x, 225.0f}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Assets and Files");
    ImGui::BeginDisabled(m_Session.inPlayMode());
    ImGui::InputText("Scene path", m_ScenePath.data(), m_ScenePath.size());
    ImGui::SameLine(); if (ImGui::Button("Save")) saveScene();
    ImGui::SameLine(); if (ImGui::Button("Load")) loadScene();
    ImGui::InputText("Asset root", m_AssetRoot.data(), m_AssetRoot.size());
    ImGui::InputText("glTF asset", m_ImportPath.data(), m_ImportPath.size());
    ImGui::SameLine(); if (ImGui::Button("Import")) importAsset();
    ImGui::InputText("Runtime binary", m_RuntimeBinary.data(), m_RuntimeBinary.size());
    ImGui::InputText("Export directory", m_ExportDirectory.data(), m_ExportDirectory.size());
    ImGui::SameLine(); if (ImGui::Button("Export")) exportRuntime();
    ImGui::EndDisabled();
    ImGui::End();
}

void EditorUi::updateCamera(float aspectRatio) {
    const Vec3 direction{std::cos(m_CameraPitch) * std::cos(m_CameraYaw), std::sin(m_CameraPitch),
                         std::cos(m_CameraPitch) * std::sin(m_CameraYaw)};
    m_View = TinyEngine::lookAt(m_CameraPosition, m_CameraPosition + direction);
    m_Projection = perspectiveVulkan(55.0f * DegreesToRadians, std::max(aspectRatio, 0.01f),
                                     0.05f, 1000.0f);
}

void EditorUi::drawViewport(float deltaSeconds) {
    const auto* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos({viewport->WorkPos.x + 245.0f, viewport->WorkPos.y + 75.0f}, ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize({std::max(200.0f, viewport->WorkSize.x - 560.0f), std::max(200.0f, viewport->WorkSize.y - 310.0f)}, ImGuiCond_FirstUseEver);
    ImGui::Begin("Viewport", nullptr, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoBackground);
    if (ImGui::RadioButton("Move", m_GizmoOperation == 0)) m_GizmoOperation = 0;
    ImGui::SameLine(); if (ImGui::RadioButton("Rotate", m_GizmoOperation == 1)) m_GizmoOperation = 1;
    ImGui::SameLine(); if (ImGui::RadioButton("Scale", m_GizmoOperation == 2)) m_GizmoOperation = 2;
    ImGui::SameLine(); ImGui::Checkbox("Local", &m_LocalGizmo);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size = ImGui::GetContentRegionAvail();
    m_Viewport = {origin.x, origin.y, std::max(size.x, 1.0f), std::max(size.y, 1.0f)};
    ImGui::InvisibleButton("SceneSurface", {m_Viewport.Width, m_Viewport.Height}, ImGuiButtonFlags_MouseButtonRight);
    const bool sceneHovered = ImGui::IsItemHovered();
    if (sceneHovered && ImGui::IsMouseDown(ImGuiMouseButton_Right) && !ImGuizmo::IsUsing()) {
        const auto& io = ImGui::GetIO();
        m_CameraYaw += io.MouseDelta.x * 0.004f;
        m_CameraPitch = std::clamp(m_CameraPitch - io.MouseDelta.y * 0.004f, -1.5f, 1.5f);
        const Vec3 forward{std::cos(m_CameraPitch) * std::cos(m_CameraYaw), std::sin(m_CameraPitch),
                           std::cos(m_CameraPitch) * std::sin(m_CameraYaw)};
        const Vec3 right = cross(forward, {0.0f, 1.0f, 0.0f}).normalized();
        const float speed = std::clamp(deltaSeconds, 0.0f, 0.1f) * (io.KeyShift ? 15.0f : 5.0f);
        if (ImGui::IsKeyDown(ImGuiKey_W)) m_CameraPosition += forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_S)) m_CameraPosition = m_CameraPosition - forward * speed;
        if (ImGui::IsKeyDown(ImGuiKey_D)) m_CameraPosition += right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_A)) m_CameraPosition = m_CameraPosition - right * speed;
        if (ImGui::IsKeyDown(ImGuiKey_E)) m_CameraPosition.y += speed;
        if (ImGui::IsKeyDown(ImGuiKey_Q)) m_CameraPosition.y -= speed;
    }
    updateCamera(m_Viewport.Width / m_Viewport.Height);
    ImGuizmo::SetDrawlist(ImGui::GetWindowDrawList());
    ImGuizmo::SetRect(m_Viewport.X, m_Viewport.Y, m_Viewport.Width, m_Viewport.Height);
    ImGuizmo::SetOrthographic(false);
    auto* transform = m_Engine.scene().registry().tryGet<TransformComponent>(m_Session.selection());
    if (transform && !m_Session.inPlayMode() && (sceneHovered || ImGuizmo::IsUsing())) {
        Mat4 matrix = transform->localMatrix();
        const ImGuizmo::OPERATION operation = m_GizmoOperation == 0 ? ImGuizmo::TRANSLATE :
            (m_GizmoOperation == 1 ? ImGuizmo::ROTATE : ImGuizmo::SCALE);
        if (ImGuizmo::Manipulate(m_View.values.data(), m_Projection.values.data(), operation,
                                m_LocalGizmo ? ImGuizmo::LOCAL : ImGuizmo::WORLD, matrix.values.data())) {
            float translation[3]{};
            float rotation[3]{};
            float scale[3]{};
            ImGuizmo::DecomposeMatrixToComponents(matrix.values.data(), translation, rotation, scale);
            m_Session.applyGizmoTransform({translation[0], translation[1], translation[2]},
                Quat::fromEulerRadians({rotation[0] * DegreesToRadians, rotation[1] * DegreesToRadians, rotation[2] * DegreesToRadians}),
                {scale[0], scale[1], scale[2]});
        }
    }
    ImGui::End();
}

void EditorUi::draw(float deltaSeconds) {
    ImGuizmo::BeginFrame();
    drawWorkspace();
    drawHierarchy();
    drawInspector();
    drawAssets();
    DrawRendering();
    drawViewport(deltaSeconds);
    const auto& io = ImGui::GetIO();
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_S, false) && !m_Session.inPlayMode()) saveScene();
}

} // namespace TinyEngine

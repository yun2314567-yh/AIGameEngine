#include "TinyEngine/Editor/Automation.h"
#include "TinyEngine/Physics.h"
#include "TinyEngine/Platform/Paths.h"
#include "TinyEngine/Renderer/GraphicsContext.h"
#include "ThirdPartyNotices.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <fstream>
#include <set>
#include <vector>

namespace TinyEngine {
namespace {

template <typename Component>
AutomationResult addComponent(Scene& scene, Entity entity, const Component& component) {
    if (!scene.valid(entity)) return AutomationResult::failure("Entity handle is not valid");
    if (scene.registry().has<Component>(entity)) return AutomationResult::failure("Entity already has requested component");
    scene.registry().emplace<Component>(entity, component);
    return AutomationResult::success();
}

AutomationResult writeTextFile(const std::filesystem::path& path, const std::string& content) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) return AutomationResult::failure("Could not open output file: " + path.string());
    output << content;
    output.close();
    if (!output) return AutomationResult::failure("Could not write output file: " + path.string());
    return AutomationResult::success();
}

bool hasControlCharacter(const std::string& value) {
    return std::any_of(value.begin(), value.end(), [](unsigned char character) {
        return character < 0x20 || character == 0x7f;
    });
}

bool isPortablePathComponent(const std::string& value) {
    if (value.empty() || value == "." || value == ".." || value.back() == ' ' || value.back() == '.' ||
        value.find_first_of("<>:\"/\\|?*") != std::string::npos || hasControlCharacter(value)) {
        return false;
    }
    std::string stem = value.substr(0, value.find('.'));
    std::transform(stem.begin(), stem.end(), stem.begin(), [](unsigned char character) {
        return static_cast<char>(std::toupper(character));
    });
    if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL") return false;
    return !(stem.size() == 4 && (stem.compare(0, 3, "COM") == 0 || stem.compare(0, 3, "LPT") == 0) && stem[3] >= '1' && stem[3] <= '9');
}

bool isSingleFileName(const std::string& value) {
    if (!isPortablePathComponent(value)) return false;
    const std::filesystem::path path(value);
    return !path.has_root_name() && !path.has_root_directory() && !path.has_parent_path() && path.filename() == path;
}

std::string comparablePath(std::filesystem::path path) {
    std::string value = path.lexically_normal().generic_string();
#ifdef _WIN32
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
#endif
    return value;
}

bool isWithinPath(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty()) return comparablePath(candidate) == comparablePath(root);
    if (relative.is_absolute()) return false;
    for (const auto& component : relative) if (component == "..") return false;
    return true;
}

bool parseSafeRelativePath(const std::string& value, std::filesystem::path& relative) {
    if (value.empty() || value.find('\0') != std::string::npos || hasControlCharacter(value) || value.find(':') != std::string::npos) {
        return false;
    }
    std::string portable = value;
    std::replace(portable.begin(), portable.end(), '\\', '/');
    if (portable.empty() || portable.front() == '/') return false;
    const std::filesystem::path parsed(portable);
    if (parsed.is_absolute() || parsed.has_root_name() || parsed.has_root_directory()) return false;
    for (const auto& component : parsed) if (!isPortablePathComponent(component.string())) return false;
    relative = parsed.lexically_normal();
    return !relative.empty() && relative != ".";
}

struct ExportAsset {
    std::filesystem::path RelativePath;
    std::filesystem::path SourcePath;
};

struct StagingDirectory {
    std::filesystem::path Path;
    ~StagingDirectory() {
        if (Path.empty()) return;
        std::error_code ignored;
        std::filesystem::remove_all(Path, ignored);
    }
};

bool createStagingDirectory(const std::filesystem::path& parent, const std::filesystem::path& output,
                            std::filesystem::path& staging, std::string& error) {
    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        staging = parent / (output.filename().string() + ".tinyengine-stage-" + std::to_string(timestamp) + "-" + std::to_string(id));
        std::error_code filesystemError;
        if (std::filesystem::create_directory(staging, filesystemError)) return true;
        if (filesystemError && filesystemError != std::errc::file_exists) {
            error = filesystemError.message();
            return false;
        }
    }
    error = "Could not allocate a unique staging directory";
    return false;
}

AutomationResult publishPackage(const std::filesystem::path& staging, const std::filesystem::path& output) {
    std::error_code error;
    const bool outputExists = std::filesystem::exists(output, error);
    if (error) return AutomationResult::failure("Could not inspect existing export directory: " + error.message());
    if (!outputExists) {
        std::filesystem::rename(staging, output, error);
        if (error) return AutomationResult::failure("Could not publish export package: " + error.message());
        return AutomationResult::success();
    }

    static std::atomic_uint64_t sequence{0};
    const auto timestamp = std::chrono::steady_clock::now().time_since_epoch().count();
    std::filesystem::path backup;
    for (int attempt = 0; attempt < 64; ++attempt) {
        const auto id = sequence.fetch_add(1, std::memory_order_relaxed);
        backup = output.parent_path() / (output.filename().string() + ".tinyengine-backup-" + std::to_string(timestamp) + "-" + std::to_string(id));
        const bool exists = std::filesystem::exists(backup, error);
        if (error) return AutomationResult::failure("Could not inspect export backup path: " + error.message());
        if (!exists) break;
        backup.clear();
    }
    if (backup.empty()) return AutomationResult::failure("Could not allocate a unique export backup path");

    std::filesystem::rename(output, backup, error);
    if (error) return AutomationResult::failure("Could not preserve existing export package: " + error.message());
    std::filesystem::rename(staging, output, error);
    if (error) {
        const std::string publishError = error.message();
        std::error_code restoreError;
        std::filesystem::rename(backup, output, restoreError);
        if (restoreError) {
            return AutomationResult::failure("Could not publish export package (" + publishError + ") or restore the previous package (" + restoreError.message() + "); previous package is at " + backup.string());
        }
        return AutomationResult::failure("Could not publish export package: " + publishError);
    }
    std::error_code ignored;
    std::filesystem::remove_all(backup, ignored);
    return AutomationResult::success();
}

} // namespace

AutomationResult EditorAutomation::requireEntity(Entity entity) const {
    return m_Scene.valid(entity) ? AutomationResult::success() : AutomationResult::failure("Entity handle is not valid");
}

AutomationResult EditorAutomation::destroyEntity(Entity entity) {
    const auto result = requireEntity(entity);
    if (!result) return result;
    return m_Scene.destroyEntity(entity) ? AutomationResult::success() : AutomationResult::failure("Entity could not be destroyed");
}

AutomationResult EditorAutomation::setTransform(Entity entity, const Vec3& translation, const Quat& rotation, const Vec3& scale) {
    const auto result = requireEntity(entity);
    if (!result) return result;
    auto* transform = m_Scene.registry().tryGet<TransformComponent>(entity);
    if (!transform) return AutomationResult::failure("Entity has no TransformComponent");
    const float values[] = {translation.x, translation.y, translation.z, rotation.x, rotation.y, rotation.z, rotation.w, scale.x, scale.y, scale.z};
    for (const float value : values) if (!std::isfinite(value)) return AutomationResult::failure("Transform values must be finite");
    const double norm = std::sqrt(static_cast<double>(rotation.x) * rotation.x + static_cast<double>(rotation.y) * rotation.y +
        static_cast<double>(rotation.z) * rotation.z + static_cast<double>(rotation.w) * rotation.w);
    if (norm == 0) return AutomationResult::failure("Transform rotation must be nonzero");
    transform->Translation = translation;
    transform->Rotation = {static_cast<float>(rotation.x / norm), static_cast<float>(rotation.y / norm),
        static_cast<float>(rotation.z / norm), static_cast<float>(rotation.w / norm)};
    transform->Scale = scale;
    return AutomationResult::success();
}

AutomationResult EditorAutomation::addCamera(Entity entity, const CameraComponent& component) {
    if (!std::isfinite(component.FieldOfView) || component.FieldOfView <= 0 || component.FieldOfView >= 180 ||
        !std::isfinite(component.NearClip) || component.NearClip <= 0 || !std::isfinite(component.FarClip) || component.FarClip <= component.NearClip)
        return AutomationResult::failure("Camera requires finite FOV in (0,180) and ordered positive clipping planes");
    return addComponent(m_Scene, entity, component);
}
AutomationResult EditorAutomation::addMeshRenderer(Entity entity, const MeshRendererComponent& component) { return addComponent(m_Scene, entity, component); }
AutomationResult EditorAutomation::addLight(Entity entity, const LightComponent& component) {
    std::string error;
    if (!ValidateLight(component, &error)) return AutomationResult::failure(error);
    return addComponent(m_Scene, entity, component);
}
AutomationResult EditorAutomation::addRigidBody(Entity entity, const RigidBodyComponent& component) {
    std::string error;
    if (!ValidateRigidBody(component, &error)) return AutomationResult::failure(error);
    return addComponent(m_Scene, entity, component);
}
AutomationResult EditorAutomation::addBoxCollider(Entity entity, const BoxColliderComponent& component) {
    std::string error;
    if (!ValidateBoxCollider(component, &error)) return AutomationResult::failure(error);
    return addComponent(m_Scene, entity, component);
}
AutomationResult EditorAutomation::addAudioSource(Entity entity, const AudioSourceComponent& component) {
    if (!std::isfinite(component.Volume) || component.Volume < 0 || !std::isfinite(component.Pitch) || component.Pitch <= 0)
        return AutomationResult::failure("Audio source requires finite nonnegative volume and positive pitch");
    return addComponent(m_Scene, entity, component);
}

AutomationResult EditorAutomation::attachScript(Entity entity, std::string module) {
    const auto result = requireEntity(entity);
    if (!result) return result;
    if (module.empty()) return AutomationResult::failure("Script module must not be empty");
    if (m_Scene.registry().has<ScriptComponent>(entity)) return AutomationResult::failure("Entity already has ScriptComponent");
    m_Scene.registry().emplace<ScriptComponent>(entity, std::move(module));
    return AutomationResult::success();
}

AutomationResult EditorAutomation::importGltf(const std::filesystem::path& path, GltfAsset& asset,
                                               const GltfImportOptions& options) {
    std::string error;
    return GltfImporter::loadFromFile(path.string(), asset, &error, options)
        ? AutomationResult::success()
        : AutomationResult::failure(error.empty() ? "glTF import failed" : std::move(error));
}

AutomationResult EditorAutomation::saveScene(const std::filesystem::path& path) const {
    return m_Scene.save(path.string()) ? AutomationResult::success() : AutomationResult::failure("Could not save scene: " + path.string());
}

AutomationResult EditorAutomation::loadScene(const std::filesystem::path& path) {
    std::string error;
    std::string text;
    if (!ReadTextFile(path, 16 * 1024 * 1024, text, &error)) return AutomationResult::failure(error);
    Scene loaded;
    if (!Scene::tryDeserialize(text, loaded, error)) return AutomationResult::failure(error);
    m_Scene = std::move(loaded);
    return AutomationResult::success();
}

AutomationResult EditorAutomation::exportRuntime(const RuntimeExportOptions& options) const {
    if (options.RuntimeBinary.empty()) return AutomationResult::failure("RuntimeBinary is required for export");
    Scene validated;
    std::string validationError;
    if (!Scene::tryDeserialize(m_Scene.serialize(), validated, validationError))
        return AutomationResult::failure("Cannot export invalid scene: " + validationError);
    if (!isSingleFileName(options.SceneFileName)) {
        return AutomationResult::failure("SceneFileName must be a single file name");
    }
    if (options.OutputDirectory.empty()) return AutomationResult::failure("OutputDirectory is required for export");
    std::error_code error;
    if (!std::filesystem::is_regular_file(options.RuntimeBinary, error)) {
        return AutomationResult::failure("Runtime binary does not exist: " + options.RuntimeBinary.string());
    }
    const auto runtimeName = options.RuntimeBinary.filename();
    const auto sceneName = comparablePath(options.SceneFileName);
    const auto noticesName = comparablePath("THIRD_PARTY_NOTICES.txt");
    if (!isSingleFileName(runtimeName.string()) || sceneName == comparablePath("tinyengine.manifest") ||
        sceneName == noticesName || comparablePath(runtimeName) == noticesName ||
        sceneName == comparablePath(runtimeName) || comparablePath(runtimeName) == comparablePath("tinyengine.manifest")) {
        return AutomationResult::failure("SceneFileName must not collide with the runtime binary or export manifest");
    }
    const auto runtimeSource = std::filesystem::canonical(options.RuntimeBinary, error);
    if (error) return AutomationResult::failure("Could not resolve runtime binary path: " + error.message());

    auto output = std::filesystem::absolute(options.OutputDirectory, error).lexically_normal();
    if (error) return AutomationResult::failure("Could not resolve output directory: " + error.message());
    if (output.filename().empty() || output == output.root_path()) return AutomationResult::failure("OutputDirectory must name a package directory");
    auto outputStatus = std::filesystem::symlink_status(output, error);
    if (error == std::errc::no_such_file_or_directory) error.clear();
    if (error) return AutomationResult::failure("Could not inspect output directory: " + error.message());
    if (std::filesystem::is_symlink(outputStatus)) return AutomationResult::failure("OutputDirectory must not be a symbolic link");
    if (std::filesystem::exists(outputStatus) && !std::filesystem::is_directory(outputStatus)) {
        return AutomationResult::failure("OutputDirectory exists and is not a directory");
    }
    const auto outputCanonical = std::filesystem::weakly_canonical(output, error);
    if (error) return AutomationResult::failure("Could not resolve output directory: " + error.message());
    const auto outputParentCanonical = std::filesystem::weakly_canonical(output.parent_path(), error);
    if (error) return AutomationResult::failure("Could not resolve output parent directory: " + error.message());
    if (comparablePath(outputCanonical) != comparablePath(outputParentCanonical / output.filename())) {
        return AutomationResult::failure("OutputDirectory must not redirect to another directory");
    }
    if (comparablePath(runtimeSource) == comparablePath(output / runtimeName) || isWithinPath(runtimeSource, outputCanonical)) {
        return AutomationResult::failure("OutputDirectory must not contain or overwrite the source runtime binary");
    }
    output = outputCanonical;

    auto assetRootPath = options.AssetRoot;
    if (assetRootPath.empty()) assetRootPath = std::filesystem::current_path(error);
    if (error) return AutomationResult::failure("Could not resolve the current working directory: " + error.message());
    const auto assetRoot = std::filesystem::canonical(assetRootPath, error);
    if (error || !std::filesystem::is_directory(assetRoot, error)) {
        return AutomationResult::failure("AssetRoot must be an existing directory" + (error ? ": " + error.message() : std::string{}));
    }
    if (isWithinPath(assetRoot, outputCanonical)) {
        return AutomationResult::failure("OutputDirectory must not contain or replace AssetRoot");
    }

    const std::set<std::string> reservedPaths{
        comparablePath(runtimeName), comparablePath(options.SceneFileName), comparablePath("tinyengine.manifest"), noticesName};
    std::set<std::string> referencedPaths;
    std::vector<ExportAsset> assets;
    const auto addFileReference = [&](const std::string& reference, const std::filesystem::path& sourceRoot,
                                      const std::filesystem::path& sourceRelative) -> AutomationResult {
        if (reference.empty()) return AutomationResult::success();
        std::filesystem::path relative;
        if (!parseSafeRelativePath(reference, relative)) {
            return AutomationResult::failure("Referenced asset path must be a safe relative path: " + reference);
        }
        const std::string key = comparablePath(relative);
        for (const auto& reserved : reservedPaths) {
            const bool overlaps = key == reserved ||
                                  (key.size() > reserved.size() && key.compare(0, reserved.size(), reserved) == 0 && key[reserved.size()] == '/') ||
                                  (reserved.size() > key.size() && reserved.compare(0, key.size(), key) == 0 && reserved[key.size()] == '/');
            if (overlaps) return AutomationResult::failure("Referenced asset path collides with a package file: " + reference);
        }
        for (const auto& existing : referencedPaths) {
            const bool overlaps = (key.size() > existing.size() && key.compare(0, existing.size(), existing) == 0 && key[existing.size()] == '/') ||
                                  (existing.size() > key.size() && existing.compare(0, key.size(), key) == 0 && existing[key.size()] == '/');
            if (overlaps) return AutomationResult::failure("Referenced asset paths overlap in the package: " + reference);
        }
        const bool duplicate = !referencedPaths.insert(key).second;

        const auto source = std::filesystem::weakly_canonical(sourceRoot / sourceRelative, error);
        if (error) return AutomationResult::failure("Could not resolve referenced asset '" + reference + "': " + error.message());
        if (!isWithinPath(source, sourceRoot) || comparablePath(source) == comparablePath(sourceRoot)) {
            return AutomationResult::failure("Referenced asset resolves outside AssetRoot: " + reference);
        }
        const bool exists = std::filesystem::exists(source, error);
        if (error) return AutomationResult::failure("Could not inspect referenced asset '" + reference + "': " + error.message());
        if (!exists) return AutomationResult::failure("Referenced file does not exist: " + reference);
        if (isWithinPath(source, outputCanonical)) return AutomationResult::failure("Export source is inside OutputDirectory: " + reference);
        if (!std::filesystem::is_regular_file(source, error) || error) {
            return AutomationResult::failure("Referenced asset is not a regular file: " + reference);
        }
        if (duplicate) {
            const auto existing = std::find_if(assets.begin(), assets.end(), [&](const ExportAsset& item) {
                return comparablePath(item.RelativePath) == key;
            });
            if (existing == assets.end() || comparablePath(existing->SourcePath) != comparablePath(source))
                return AutomationResult::failure("Different sources collide in package: " + reference);
            return AutomationResult::success();
        }
        assets.push_back({relative, source});
        return AutomationResult::success();
    };
    const auto addAssetReference = [&](const std::string& reference) {
        std::filesystem::path relative;
        if (!reference.empty() && !parseSafeRelativePath(reference, relative))
            return AutomationResult::failure("Referenced asset path must be a safe relative path: " + reference);
        return addFileReference(reference, assetRoot, relative);
    };

    AutomationResult referenceResult = addAssetReference(m_Scene.Environment().EquirectangularPath);
    const auto& registry = m_Scene.registry();
    for (const Entity entity : registry.entities()) {
        if (const auto* component = registry.tryGet<ScriptComponent>(entity); component && referenceResult) {
            referenceResult = addAssetReference(component->Module);
        }
        if (const auto* component = registry.tryGet<AudioSourceComponent>(entity); component && referenceResult) {
            referenceResult = addAssetReference(component->Clip);
        }
        if (const auto* component = registry.tryGet<MeshRendererComponent>(entity); component && referenceResult) {
            referenceResult = addAssetReference(component->Mesh);
            if (referenceResult && !component->Mesh.empty()) {
                GltfAsset imported;
                std::string importError;
                if (!GltfImporter::loadFromFile((assetRoot / component->Mesh).string(), imported, &importError, {true, assetRoot}))
                    return AutomationResult::failure("Cannot export mesh '" + component->Mesh + "': " + importError);
                for (const auto& dependency : imported.ExternalFiles) {
                    const auto relative = (std::filesystem::path(component->Mesh).parent_path() / dependency).lexically_normal();
                    referenceResult = addAssetReference(relative.generic_string());
                    if (!referenceResult) return referenceResult;
                }
            }
            if (referenceResult) referenceResult = addAssetReference(component->Material);
            if (referenceResult && !component->Material.empty()) {
                PbrMaterial material;
                std::string materialError;
                if (!LoadPbrMaterialOverride(assetRoot / component->Material, material, &materialError))
                    return AutomationResult::failure("Cannot export material '" + component->Material + "': " + materialError);
            }
        }
    }
    if (!referenceResult) return referenceResult;

    auto shaderDirectory = options.ShaderDirectory;
    if (shaderDirectory.empty() && GraphicsContext::available()) shaderDirectory = FindShaderDirectory(runtimeSource);
    if (GraphicsContext::available() && shaderDirectory.empty())
        return AutomationResult::failure("Compiled shaders are required for a Vulkan runtime export");
    if (!shaderDirectory.empty()) {
        shaderDirectory = std::filesystem::canonical(shaderDirectory, error);
        if (error) return AutomationResult::failure("Cannot resolve ShaderDirectory: " + error.message());
        constexpr const char* shaderNames[] = {"Depth.vert", "Depth.frag", "Shadow.vert", "Shadow.frag",
            "Pbr.vert", "Pbr.frag", "Ssao.comp", "SsaoBlur.comp", "BufferTransform.comp", "ToneMap.comp", "Present.vert", "Present.frag"};
        for (const auto* shaderName : shaderNames) {
            const std::string filename = std::string(shaderName) + ".hlsl.spv";
            const auto result = addFileReference("shaders/" + filename, shaderDirectory, filename);
            if (!result) return result;
        }
    }
    for (const auto& dependency : options.RuntimeDependencies) {
        const auto source = std::filesystem::canonical(dependency, error);
        if (error) return AutomationResult::failure("Cannot resolve runtime dependency: " + dependency.string());
        const auto result = addFileReference(source.filename().string(), source.parent_path(), source.filename());
        if (!result) return result;
    }
    std::sort(assets.begin(), assets.end(), [](const ExportAsset& lhs, const ExportAsset& rhs) {
        return comparablePath(lhs.RelativePath) < comparablePath(rhs.RelativePath);
    });

    const auto parent = output.parent_path();
    std::filesystem::create_directories(parent, error);
    if (error) return AutomationResult::failure("Could not create export parent directory: " + error.message());
    std::filesystem::path stagingPath;
    std::string stagingError;
    if (!createStagingDirectory(parent, output, stagingPath, stagingError)) {
        return AutomationResult::failure("Could not create export staging directory: " + stagingError);
    }
    StagingDirectory staging{stagingPath};

    std::filesystem::copy_file(runtimeSource, stagingPath / runtimeName, std::filesystem::copy_options::none, error);
    if (error) return AutomationResult::failure("Could not stage runtime binary: " + error.message());
    auto result = writeTextFile(stagingPath / options.SceneFileName, m_Scene.serialize());
    if (!result) return result;
    result = writeTextFile(stagingPath / "THIRD_PARTY_NOTICES.txt", ThirdPartyNotices);
    if (!result) return result;

    std::string manifest = "runtime=" + runtimeName.string() + "\nscene=" + options.SceneFileName +
        "\nasset=THIRD_PARTY_NOTICES.txt\n";
    for (const auto& asset : assets) {
        const auto destination = stagingPath / asset.RelativePath;
        std::filesystem::create_directories(destination.parent_path(), error);
        if (error) return AutomationResult::failure("Could not create asset package directory: " + error.message());
        std::filesystem::copy_file(asset.SourcePath, destination, std::filesystem::copy_options::none, error);
        if (error) return AutomationResult::failure("Could not stage referenced asset '" + asset.RelativePath.generic_string() + "': " + error.message());
        manifest += "asset=" + asset.RelativePath.generic_string() + "\n";
    }
    result = writeTextFile(stagingPath / "tinyengine.manifest", manifest);
    if (!result) return result;
    return publishPackage(stagingPath, output);
}

} // namespace TinyEngine

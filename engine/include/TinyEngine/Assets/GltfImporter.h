#pragma once

#include "TinyEngine/Math.h"
#include "TinyEngine/Renderer/Renderer.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace TinyEngine {

struct GltfImage {
    std::string Name;
    std::string Uri;
    std::string MimeType;
    std::vector<std::uint8_t> Data; // Validated embedded or loaded image payload.
};

struct GltfVertex {
    Vec3 Position{};
    Vec3 Normal{};
    Vec3 Tangent{};
    float TangentSign = 1.0f;
    float U = 0.0f;
    float V = 0.0f;
};

struct GltfTexture {
    std::int32_t ImageIndex = -1;
    std::string Name;
};

struct GltfAccessor {
    std::int32_t BufferView = -1;
    std::uint32_t ByteOffset = 0;
    std::uint32_t Count = 0;
    std::string Type;
    std::uint32_t ComponentType = 0;
    bool Normalized = false;
};

struct GltfPrimitive {
    std::int32_t PositionAccessor = -1;
    std::int32_t NormalAccessor = -1;
    std::int32_t TangentAccessor = -1;
    std::int32_t Texcoord0Accessor = -1;
    std::int32_t IndicesAccessor = -1;
    std::int32_t MaterialIndex = -1;
    std::string Mode = "TRIANGLES";
    std::vector<GltfVertex> Vertices;
    std::vector<std::uint32_t> Indices;
};

struct GltfMesh {
    std::string Name;
    std::vector<GltfPrimitive> Primitives;
};

struct GltfNode {
    std::string Name;
    std::int32_t MeshIndex = -1;
    std::int32_t SkinIndex = -1;
    std::int32_t ParentIndex = -1;
    Vec3 Translation{};
    Quat Rotation{};
    Vec3 Scale{1.0f, 1.0f, 1.0f};
    Mat4 Matrix = Mat4::identity();
    bool HasMatrix = false;
    std::vector<std::int32_t> Children;
};

struct GltfScene {
    std::string Name;
    std::vector<std::int32_t> RootNodes;
};

struct GltfAsset {
    std::string SourcePath;
    std::string Generator;
    // Decoded, normalized buffer/image paths relative to SourcePath's directory.
    // Data URIs and embedded GLB payloads do not appear here.
    std::vector<std::filesystem::path> ExternalFiles;
    std::vector<GltfImage> Images;
    std::vector<GltfTexture> Textures;
    std::vector<PbrMaterial> Materials;
    std::vector<GltfAccessor> Accessors;
    std::vector<GltfMesh> Meshes;
    std::vector<GltfNode> Nodes;
    std::vector<GltfScene> Scenes;
    std::int32_t DefaultScene = -1;
};

struct GltfImportOptions {
    // When true, import fails if declared buffer payloads cannot be loaded and decoded.
    bool RequireExternalPayloads = false;
    // External files must remain inside this directory after resolving symlinks.
    // Empty uses the source document's directory. No network URIs are loaded.
    std::filesystem::path AssetRoot;
};

class GltfImporter {
public:
    // Synchronous; independent calls may run concurrently. On failure asset is
    // unchanged and error describes the rejected input. Files are limited to
    // 256 MiB each; unsupported compressed/sparse geometry fails explicitly.
    static bool loadFromFile(const std::string& path, GltfAsset& asset, std::string* error = nullptr,
                             const GltfImportOptions& options = {});
    static bool loadFromString(std::string_view json, GltfAsset& asset, std::string* error = nullptr,
                               const GltfImportOptions& options = {});
};

} // namespace TinyEngine

#include "TinyEngine/Assets/GltfImporter.h"
#include "TinyEngine/Editor/EngineHost.h"
#include "TinyEngine/Renderer/GraphicsContext.h"
#include "TinyEngine/Renderer/Renderer.h"
#include "TinyEngine/Renderer/ShaderTypes.h"
#include "TinyEngine/Renderer/Environment.h"

#include <algorithm>
#include <bit>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

namespace {
void appendU32(std::vector<std::uint8_t> &bytes, std::uint32_t value) {
  bytes.push_back(static_cast<std::uint8_t>(value & 0xffu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 8u) & 0xffu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 16u) & 0xffu));
  bytes.push_back(static_cast<std::uint8_t>((value >> 24u) & 0xffu));
}
void appendFloat(std::vector<std::uint8_t> &bytes, float value) {
  appendU32(bytes, std::bit_cast<std::uint32_t>(value));
}
std::vector<std::uint8_t> makeInterleavedMeshBuffer() {
  std::vector<std::uint8_t> bytes(4, 0xaa);
  constexpr float vertices[][8] = {
      {0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f},
      {1.0f, 0.0f, 0.0f, 0.0f, 0.0f, 1.0f, 1.0f, 0.0f},
      {0.0f, 1.0f, 0.0f, 0.0f, 0.0f, 1.0f, 0.0f, 1.0f},
  };
  for (const auto &vertex : vertices)
    for (const float value : vertex)
      appendFloat(bytes, value);
  bytes.push_back(0xcc);
  bytes.push_back(0xcc);
  bytes.push_back(0);
  bytes.push_back(0);
  bytes.push_back(1);
  bytes.push_back(0);
  bytes.push_back(2);
  bytes.push_back(0);
  return bytes;
}
std::string encodeBase64(const std::vector<std::uint8_t> &bytes) {
  constexpr char alphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string encoded;
  for (std::size_t index = 0; index < bytes.size(); index += 3) {
    const std::uint32_t first = bytes[index];
    const std::uint32_t second =
        index + 1 < bytes.size() ? bytes[index + 1] : 0u;
    const std::uint32_t third =
        index + 2 < bytes.size() ? bytes[index + 2] : 0u;
    const std::uint32_t value = (first << 16u) | (second << 8u) | third;
    encoded.push_back(alphabet[(value >> 18u) & 0x3fu]);
    encoded.push_back(alphabet[(value >> 12u) & 0x3fu]);
    encoded.push_back(index + 1 < bytes.size() ? alphabet[(value >> 6u) & 0x3fu]
                                               : '=');
    encoded.push_back(index + 2 < bytes.size() ? alphabet[value & 0x3fu] : '=');
  }
  return encoded;
}
std::string meshDocument(std::string_view bufferUri) {
  return std::string(
             R"json({      "asset":{"version":"2.0"},      "buffers":[{"uri":")json") +
         std::string(bufferUri) +
         R"json(","byteLength":108}],      "bufferViews":[        {"buffer":0,"byteOffset":4,"byteLength":96,"byteStride":32},        {"buffer":0,"byteOffset":100,"byteLength":8}      ],      "accessors":[        {"bufferView":0,"byteOffset":0,"componentType":5126,"count":3,"type":"VEC3"},        {"bufferView":0,"byteOffset":12,"componentType":5126,"count":3,"type":"VEC3"},        {"bufferView":0,"byteOffset":24,"componentType":5126,"count":3,"type":"VEC2"},        {"bufferView":1,"byteOffset":2,"componentType":5123,"count":3,"type":"SCALAR"}      ],      "meshes":[{"name":"Triangle","primitives":[{"attributes":{"POSITION":0,"NORMAL":1,"TEXCOORD_0":2},"indices":3}]}]    })json";
}
void testRendererPipeline() {
  TinyEngine::NullRenderBackend backend;
  TinyEngine::Renderer renderer(backend);
  TinyEngine::RenderSettings settings;
  settings.EnableCompute = false;
  std::string error;
  assert(renderer.setSettings(settings, &error));
  const auto passes = renderer.buildPasses();
  assert(!passes.empty());
  assert(std::none_of(
      passes.begin(), passes.end(), [](const TinyEngine::RenderPass &pass) {
        return pass.Kind == TinyEngine::RenderPassKind::IblLighting;
      }));
  assert(!renderer.setEnvironment(TinyEngine::EnvironmentMap{}, &error));
  assert(renderer.render({1280, 720, 1.0f / 60.0f, 0}, &error));
  assert(backend.executedPassCount() == renderer.buildPasses().size());
  const auto mapped = TinyEngine::toneMap({8.0f, 1.0f, 0.0f}, 1.0f,
                                          TinyEngine::ToneMapper::Aces);
  assert(mapped.x <= 1.0f && mapped.x >= 0.0f && std::isfinite(mapped.x));
  assert(TinyEngine::generateSsaoKernel(32).size() == 32);
  assert(!TinyEngine::validatePbrMaterial(
      TinyEngine::PbrMaterial{TinyEngine::Vec3{1.0f, 1.0f, 1.0f}, 2.0f},
      &error));
}
void testMaterialOverrides() {
  using namespace TinyEngine;
  PbrMaterial material;
  material.BaseColorTexture = 3;
  material.NormalTexture = 5;
  std::string error;
  assert(ApplyPbrMaterialOverride(
      "TINYENGINE_MATERIAL 1 base_color 0.2 0.4 0.6 0.75 metallic 0.8 roughness 0.3 "
      "normal_scale 0.5 occlusion_strength 0.4 emissive 2 1 0 alpha_cutoff 0.2 alpha_mode blend double_sided 1",
      material, &error));
  assert(material.BaseColorTexture == 3 && material.NormalTexture == 5);
  assert(nearlyEqual(material.BaseColor, {0.2f, 0.4f, 0.6f}));
  assert(material.BaseAlpha == 0.75f && material.Metallic == 0.8f && material.Roughness == 0.3f);
  assert(material.Alpha == PbrMaterial::AlphaMode::Blend && material.DoubleSided);
  for (const auto text : {"", "TINYENGINE_MATERIAL 2", "TINYENGINE_MATERIAL 1 roughness -1",
                         "TINYENGINE_MATERIAL 1 roughness 0.5 roughness 0.2",
                         "TINYENGINE_MATERIAL 1 alpha_mode unknown",
                         "TINYENGINE_MATERIAL 1 base_color 1 1", "TINYENGINE_MATERIAL 1 extra 2",
                         "TINYENGINE_MATERIAL 1 metallic 1.1", "TINYENGINE_MATERIAL 1 double_sided 2",
                         "TINYENGINE_MATERIAL 1 base_color 1 1 1 -0.5"}) {
    assert(!ApplyPbrMaterialOverride(text, material, &error));
    assert(!error.empty());
    assert(material.Roughness == 0.3f && material.Metallic == 0.8f && material.BaseColorTexture == 3);
  }
  assert(!ApplyPbrMaterialOverride(std::string(65537, 'x'), material, &error));
  const auto root = std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets/materials";
  for (const auto name : {"ground.pbr", "player.pbr", "prop.pbr", "platform.pbr"})
    assert(LoadPbrMaterialOverride(root / name, material, &error));
  const auto saved = material.BaseColor;
  assert(!LoadPbrMaterialOverride(root / "missing.pbr", material, &error));
  assert(nearlyEqual(material.BaseColor, saved));
}

void testLightValidation() {
  using namespace TinyEngine;
  LightComponent light;
  std::string error;
  assert(ValidateLight(light, &error));
  light.Intensity = -1;
  assert(!ValidateLight(light, &error));
  light.Intensity = 1;
  light.Color.x = -0.1f;
  assert(!ValidateLight(light, &error));
  light.Color.x = 1;
  light.Range = std::numeric_limits<float>::infinity();
  assert(!ValidateLight(light, &error));
  light.Range = 10;
  light.OuterConeRadians = 2;
  assert(!ValidateLight(light, &error));
  light.OuterConeRadians = light.InnerConeRadians;
  assert(ValidateLight(light, &error));
  light.Type = static_cast<LightType>(-1);
  assert(!ValidateLight(light, &error));
}

void testCameraMatrices() {
  using namespace TinyEngine;
  const Mat4 projection = perspectiveVulkan(1.0f, 16.0f / 9.0f, 0.1f, 500.0f);
  Mat4 inverseProjection{};
  assert(inverse(projection, inverseProjection));
  const Mat4 identity = projection * inverseProjection;
  for (std::uint32_t column = 0; column < 4; ++column) {
    for (std::uint32_t row = 0; row < 4; ++row) {
      const float expected = column == row ? 1.0f : 0.0f;
      assert(std::fabs(identity(column, row) - expected) < 1.0e-4f);
    }
  }
  assert(!inverse(Mat4{}, inverseProjection));
  const Mat4 view = lookAt({0.0f, 0.0f, 5.0f}, {0.0f, 0.0f, 0.0f});
  const Vec3 originInView = view.transformPoint({0.0f, 0.0f, 0.0f});
  assert(nearlyEqual(originInView, {0.0f, 0.0f, -5.0f}));
  assert(nearlyEqual(orthographicVulkan(-2.0f, 2.0f, -1.0f, 1.0f, 0.1f, 10.0f)
                         .transformPoint({0, 0, -0.1f}),
                     {0, 0, 0}));
}
void testGltfMetadata() {
  constexpr std::string_view document =
      R"({      "asset": {"version": "2.0", "generator": "test"},      "materials": [{"pbrMetallicRoughness": {"baseColorFactor": [0.2, 0.4, 0.6, 1], "metallicFactor": 0.25}}],      "nodes": [{"name": "Root", "translation": [1, 2, 3]}],      "scenes": [{"name": "Main", "nodes": [0]}], "scene": 0    })";
  TinyEngine::GltfAsset asset;
  std::string error;
  assert(TinyEngine::GltfImporter::loadFromString(document, asset, &error));
  assert(asset.Generator == "test");
  assert(asset.Materials.size() == 1 && asset.Nodes.size() == 1 &&
         asset.DefaultScene == 0);
  assert(!TinyEngine::GltfImporter::loadFromString("{}", asset, &error));
  assert(asset.Generator == "test");
  assert(asset.Materials.size() == 1 && asset.Nodes.size() == 1);
  TinyEngine::GltfImportOptions options;
  options.RequireExternalPayloads = true;
  constexpr std::string_view unsupportedPayload =
      R"({      "asset": {"version": "2.0"}, "buffers": [{"uri": "mesh.bin", "byteLength": 4}]    })";
  assert(!TinyEngine::GltfImporter::loadFromString(unsupportedPayload, asset,
                                                   &error, options));
  assert(asset.Generator == "test");
  assert(asset.Materials.size() == 1 && asset.Nodes.size() == 1);
}
void testGltfMeshPayloads() {
  const auto bytes = makeInterleavedMeshBuffer();
  const std::string embeddedUri =
      "data:application/octet-stream;base64," + encodeBase64(bytes);
  TinyEngine::GltfAsset asset;
  std::string error;
  TinyEngine::GltfImportOptions options;
  options.RequireExternalPayloads = true;
  assert(TinyEngine::GltfImporter::loadFromString(meshDocument(embeddedUri),
                                                  asset, &error, options));
  assert(asset.Meshes.size() == 1 && asset.Meshes[0].Primitives.size() == 1);
  const auto &primitive = asset.Meshes[0].Primitives[0];
  assert(primitive.Vertices.size() == 3 && primitive.Indices.size() == 3);
  assert(primitive.Vertices[1].Position.x == 1.0f &&
         primitive.Vertices[2].Position.y == 1.0f);
  assert(primitive.Vertices[0].Normal.z == 1.0f);
  assert(primitive.Vertices[1].U == 1.0f && primitive.Vertices[2].V == 1.0f);
  assert((primitive.Indices == std::vector<std::uint32_t>{0, 1, 2}));
  assert(asset.Accessors[1].ByteOffset == 12 &&
         asset.Accessors[3].ByteOffset == 2);
  TinyEngine::GltfAsset showcaseAsset;
  const std::filesystem::path showcasePath =
      std::filesystem::path(TINYENGINE_SOURCE_DIR) / "assets" / "meshes" /
      "showcase.gltf";
  assert(TinyEngine::GltfImporter::loadFromFile(
      showcasePath.string(), showcaseAsset, &error, options));
  assert(showcaseAsset.Meshes.size() == 1 &&
         showcaseAsset.Meshes[0].Primitives.size() == 1);
  assert(showcaseAsset.Meshes[0].Primitives[0].Vertices.size() == 24);
  assert(showcaseAsset.Meshes[0].Primitives[0].Indices.size() == 36);
  for (const std::uint32_t componentType : {5121u, 5125u}) {
    std::vector<std::uint8_t> alternateBytes(bytes.begin(),
                                             bytes.begin() + 100);
    alternateBytes.push_back(0xcc);
    alternateBytes.push_back(0xcc);
    for (std::uint32_t index = 0; index < 3; ++index) {
      if (componentType == 5121)
        alternateBytes.push_back(static_cast<std::uint8_t>(index));
      else
        appendU32(alternateBytes, index);
    }
    const std::string alternateUri =
        "data:application/octet-stream;base64," + encodeBase64(alternateBytes);
    std::string alternateDocument = meshDocument(alternateUri);
    const auto replaceToken = [&alternateDocument](std::string_view before,
                                                   const std::string &after) {
      const auto position = alternateDocument.find(before);
      assert(position != std::string::npos);
      alternateDocument.replace(position, before.size(), after);
    };
    replaceToken("\"byteLength\":108",
                 "\"byteLength\":" + std::to_string(alternateBytes.size()));
    replaceToken(
        "\"byteLength\":8}",
        "\"byteLength\":" + std::to_string(alternateBytes.size() - 100) + "}");
    replaceToken("\"componentType\":5123",
                 "\"componentType\":" + std::to_string(componentType));
    assert(TinyEngine::GltfImporter::loadFromString(alternateDocument, asset,
                                                    &error, options));
    assert((asset.Meshes[0].Primitives[0].Indices ==
            std::vector<std::uint32_t>{0, 1, 2}));
  }
  std::string malformed = meshDocument(embeddedUri);
  const auto badOffset = malformed.find("\"byteOffset\":24");
  assert(badOffset != std::string::npos);
  malformed.replace(badOffset, std::string("\"byteOffset\":24").size(),
                    "\"byteOffset\":90");
  assert(!TinyEngine::GltfImporter::loadFromString(malformed, asset, &error));
  assert(asset.Meshes.size() == 1 &&
         asset.Meshes[0].Primitives[0].Vertices.size() == 3);
  malformed = meshDocument(embeddedUri);
  const auto invalidIndex = malformed.find("\"indices\":3");
  malformed.replace(invalidIndex, std::string("\"indices\":3").size(),
                    "\"indices\":99");
  assert(!TinyEngine::GltfImporter::loadFromString(malformed, asset, &error));
  malformed = meshDocument(embeddedUri);
  const auto sparse = malformed.find("\"bufferView\":0,\"byteOffset\":0");
  malformed.insert(sparse, "\"sparse\":{},");
  assert(!TinyEngine::GltfImporter::loadFromString(malformed, asset, &error));
  auto normalizedBytes =
      std::vector<std::uint8_t>(bytes.begin(), bytes.begin() + 100);
  normalizedBytes.insert(normalizedBytes.end(), {0, 0, 255, 0, 0, 255});
  const std::string normalizedUri =
      "data:application/octet-stream;base64," + encodeBase64(normalizedBytes);
  const std::string normalizedDocument =
      std::string(
          R"json({      "asset":{"version":"2.0"},      "buffers":[{"uri":")json") +
      normalizedUri +
      R"json(","byteLength":106}],      "bufferViews":[        {"buffer":0,"byteOffset":4,"byteLength":96,"byteStride":32},        {"buffer":0,"byteOffset":100,"byteLength":6}      ],      "accessors":[        {"bufferView":0,"componentType":5126,"count":3,"type":"VEC3"},        {"bufferView":1,"componentType":5121,"normalized":true,"count":3,"type":"VEC2"}      ],      "meshes":[{"primitives":[{"attributes":{"POSITION":0,"TEXCOORD_0":1}}]}]    })json";
  assert(TinyEngine::GltfImporter::loadFromString(normalizedDocument, asset,
                                                  &error, options));
  const auto &normalizedPrimitive = asset.Meshes[0].Primitives[0];
  assert(normalizedPrimitive.Vertices[1].U == 1.0f &&
         normalizedPrimitive.Vertices[2].V == 1.0f);
  assert((normalizedPrimitive.Indices == std::vector<std::uint32_t>{0, 1, 2}));
  const auto directory =
      std::filesystem::temp_directory_path() / "tinyengine-gltf-payload-test";
  std::error_code filesystemError;
  std::filesystem::remove_all(directory, filesystemError);
  std::filesystem::create_directories(directory, filesystemError);
  assert(!filesystemError);
  {
    std::ofstream binary(directory / "mesh.bin", std::ios::binary);
    binary.write(reinterpret_cast<const char *>(bytes.data()),
                 static_cast<std::streamsize>(bytes.size()));
    assert(binary.good());
    std::ofstream document(directory / "mesh.gltf", std::ios::binary);
    const auto externalDocument = meshDocument("mesh.bin");
    document.write(externalDocument.data(),
                   static_cast<std::streamsize>(externalDocument.size()));
    assert(document.good());
  }
  assert(TinyEngine::GltfImporter::loadFromFile(
      (directory / "mesh.gltf").string(), asset, &error, options));
  assert(asset.Meshes[0].Primitives[0].Indices.size() == 3);
  assert(asset.ExternalFiles == std::vector<std::filesystem::path>{"mesh.bin"});
  for (const std::string uri : {"data:application/octet-stream;base64,AA==",
                                "data:application/octet-stream;base64,@@@@",
                                "data:application/octet-stream;base64,AAAA=AAA"}) {
    std::ofstream document(directory / "truncated.gltf", std::ios::binary);
    document << meshDocument(uri);
    document.close();
    assert(!TinyEngine::GltfImporter::loadFromFile((directory / "truncated.gltf").string(), asset, &error, options));
    assert(asset.Meshes[0].Primitives[0].Indices.size() == 3);
  }
  for (const std::string uri : {"../outside.bin", "%2e%2e/outside.bin", "C:/outside.bin", "https://example.com/file.bin", "mesh%00.bin"}) {
    std::ofstream document(directory / "unsafe.gltf", std::ios::binary);
    document << meshDocument(uri);
    document.close();
    assert(!TinyEngine::GltfImporter::loadFromFile((directory / "unsafe.gltf").string(), asset, &error, options));
    assert(asset.Meshes[0].Primitives[0].Indices.size() == 3);
  }
  {
    auto overflowDocument = meshDocument("mesh.bin");
    const auto offset = overflowDocument.find("\"byteOffset\":4");
    assert(offset != std::string::npos);
    overflowDocument.replace(offset, std::string("\"byteOffset\":4").size(), "\"byteOffset\":18446744073709551612");
    std::ofstream document(directory / "overflow.gltf", std::ios::binary);
    document << overflowDocument;
    document.close();
    assert(!TinyEngine::GltfImporter::loadFromFile((directory / "overflow.gltf").string(), asset, &error, options));
    assert(asset.Meshes[0].Primitives[0].Indices.size() == 3);
  }
  std::filesystem::remove_all(directory, filesystemError);
}
void testEngineHost() {
  TinyEngine::NullRenderBackend backend;
  TinyEngine::Renderer renderer(backend);
  TinyEngine::Scene scene("RendererTest");
  TinyEngine::EngineHost host(scene, renderer);
  assert(host.start());
  std::string error;
  assert(host.tick(1.0f / 60.0f, 640, 480, &error));
  assert(host.frameIndex() == 1);
}
void testComputeValidation() {
  using namespace TinyEngine;
  const std::array<std::uint32_t, 3> limits{65535, 65535, 65535};
  ComputeDispatch dispatch{"BufferTransform.comp.hlsl.spv", {2, 1, 1}, {{0, {1, 2, 3}}, {1, {0, 0}}}};
  std::string error;
  assert(ValidateComputeDispatch(dispatch, limits, &error));
  dispatch.Buffers[1].Binding = 0;
  assert(!ValidateComputeDispatch(dispatch, limits, &error));
  dispatch.Buffers[1].Binding = 16;
  assert(!ValidateComputeDispatch(dispatch, limits, &error));
  dispatch.Buffers[1].Binding = 1;
  dispatch.Buffers[1].Words.clear();
  assert(!ValidateComputeDispatch(dispatch, limits, &error));
  dispatch.Buffers.clear();
  for (const auto path : {"", "../escape.spv", "C:/shader.spv", "/shader.spv"}) {
    dispatch.Shader = path;
    assert(!ValidateComputeDispatch(dispatch, limits, &error));
  }
  dispatch.Shader = "shader.spv";
  dispatch.WorkGroups[0] = 0;
  assert(!ValidateComputeDispatch(dispatch, limits, &error));
  dispatch.WorkGroups[0] = 65536;
  assert(!ValidateComputeDispatch(dispatch, limits, &error));
}
void testEnvironmentFiltering() {
  using namespace TinyEngine;
  std::vector<float> pixels(16 * 8 * 4);
  for (std::size_t index = 0; index < pixels.size(); index += 4) {
    pixels[index] = 2; pixels[index + 1] = 0.5f; pixels[index + 2] = 4; pixels[index + 3] = 1;
  }
  FilteredEnvironment filtered;
  std::string error;
  assert(FilterEnvironment(pixels, 16, 8, 8, 4, 4, filtered, &error));
  assert(filtered.SpecularMips.size() == 4 && filtered.SpecularMips.back().Width == 2);
  const auto checkConstant = [](const EnvironmentImage& image) {
    for (std::size_t index = 0; index < image.Pixels.size(); index += 4) {
      assert(std::fabs(image.Pixels[index] - 2) < 1e-4f);
      assert(std::fabs(image.Pixels[index + 1] - 0.5f) < 1e-4f);
      assert(std::fabs(image.Pixels[index + 2] - 4) < 1e-4f);
    }
  };
  for (const auto& mip : filtered.SpecularMips) checkConstant(mip);
  checkConstant(filtered.Irradiance);
  for (const auto value : filtered.Brdf.Pixels) assert(std::isfinite(value) && value >= 0 && value <= 1.1f);
  assert(filtered.Brdf.Pixels[(32 * 64 + 63) * 4] > 0.5f);
  assert(!FilterEnvironment(pixels, 0, 8, 8, 4, 4, filtered, &error));
  assert(!FilterEnvironment(pixels, 16, 8, 8, 10, 4, filtered, &error));
  pixels[0] = std::numeric_limits<float>::quiet_NaN();
  assert(!FilterEnvironment(pixels, 16, 8, 8, 4, 4, filtered, &error));
  checkConstant(filtered.Irradiance);
  pixels[0] = -1;
  assert(!FilterEnvironment(pixels, 16, 8, 8, 4, 4, filtered, &error));
  // A narrow bright source must spread into other directions after convolution.
  std::fill(pixels.begin(), pixels.end(), 0.0f);
  pixels[(4 * 16 + 8) * 4] = 100;
  assert(FilterEnvironment(pixels, 16, 8, 8, 4, 4, filtered, &error));
  const auto& rough = filtered.SpecularMips[1].Pixels;
  std::size_t illuminated = 0;
  for (std::size_t index = 0; index < rough.size(); index += 4)
    if (rough[index] > 0.01f) ++illuminated;
  assert(illuminated > 4); // More directions than the source's bilinear footprint.
}
} // namespace
void runRendererTests() {
  testRendererPipeline();
  testCameraMatrices();
  testMaterialOverrides();
  testLightValidation();
  testGltfMetadata();
  testGltfMeshPayloads();
  testEngineHost();
  testComputeValidation();
  testEnvironmentFiltering();
}

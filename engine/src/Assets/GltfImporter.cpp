#include "TinyEngine/Assets/GltfImporter.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>

#if defined(TINYENGINE_WITH_CGLTF)
#define CGLTF_IMPLEMENTATION
#include <cgltf.h>
#endif

namespace TinyEngine {
namespace {

using Range = std::pair<std::size_t, std::size_t>;
constexpr std::size_t MaxAssetFileBytes = 256u * 1024u * 1024u;
constexpr std::size_t MaxDecodedGeometryBytes = 512u * 1024u * 1024u;

bool accountGeometry(std::size_t vertices, std::size_t indices, std::size_t& total, std::string* error) {
    if (vertices > (MaxDecodedGeometryBytes - total) / sizeof(GltfVertex)) {
        if (error) *error = "Decoded glTF geometry exceeds the 512 MiB limit";
        return false;
    }
    total += vertices * sizeof(GltfVertex);
    if (indices > (MaxDecodedGeometryBytes - total) / sizeof(std::uint32_t)) {
        if (error) *error = "Decoded glTF geometry exceeds the 512 MiB limit";
        return false;
    }
    total += indices * sizeof(std::uint32_t);
    return true;
}

bool readFileBytes(const std::filesystem::path& path, std::vector<std::uint8_t>& bytes, std::string* error) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    const auto size = file ? file.tellg() : std::streampos(-1);
    if (size < 0 || static_cast<std::uint64_t>(size) > MaxAssetFileBytes) {
        if (error) *error = "Asset file is unavailable or exceeds the 256 MiB limit: " + path.string();
        return false;
    }
    bytes.resize(static_cast<std::size_t>(size));
    file.seekg(0);
    if (!bytes.empty()) file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (!file) { if (error) *error = "Could not read asset file: " + path.string(); return false; }
    return true;
}

bool isWithinRoot(const std::filesystem::path& candidate, const std::filesystem::path& root) {
    const auto relative = candidate.lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) return false;
    for (const auto& part : relative) if (part == "..") return false;
    return true;
}

bool decodeExternalUri(std::string_view uri, std::filesystem::path& relative, std::string* error) {
    std::string decoded;
    const auto hex = [](char value) -> int {
        if (value >= '0' && value <= '9') return value - '0';
        if (value >= 'a' && value <= 'f') return value - 'a' + 10;
        if (value >= 'A' && value <= 'F') return value - 'A' + 10;
        return -1;
    };
    for (std::size_t index = 0; index < uri.size(); ++index) {
        unsigned char value = static_cast<unsigned char>(uri[index]);
        if (value == '%') {
            if (index + 2 >= uri.size() || hex(uri[index + 1]) < 0 || hex(uri[index + 2]) < 0) {
                if (error) *error = "Invalid percent escape in glTF URI";
                return false;
            }
            value = static_cast<unsigned char>(hex(uri[index + 1]) * 16 + hex(uri[index + 2]));
            index += 2;
        }
        if (value < 0x20 || value == 0x7f || value == ':' || value == '\\' || value == '?' || value == '#') {
            if (error) *error = "glTF URI must name a local relative file";
            return false;
        }
        decoded.push_back(static_cast<char>(value));
    }
    relative = std::filesystem::path(decoded).lexically_normal();
    if (relative.empty() || relative == "." || relative.has_root_path()) {
        if (error) *error = "glTF URI must name a local relative file";
        return false;
    }
    return true;
}

bool resolveExternalFile(const std::filesystem::path& directory, const std::filesystem::path& root,
                         const std::filesystem::path& relative, std::filesystem::path& source, std::string* error) {
    std::error_code filesystemError;
    source = std::filesystem::weakly_canonical(directory / relative, filesystemError);
    if (filesystemError || !isWithinRoot(source, root)) {
        if (error) *error = "glTF external file resolves outside AssetRoot: " + relative.generic_string();
        return false;
    }
    return true;
}

bool addExternalFile(GltfAsset& asset, std::string_view uri, const std::filesystem::path& directory,
                     const std::filesystem::path& root, std::string* error) {
    if (uri.empty() || uri.starts_with("data:")) return true;
    std::filesystem::path relative, source;
    if (!decodeExternalUri(uri, relative, error)) return false;
    if (!directory.empty() && !resolveExternalFile(directory, root, relative, source, error)) return false;
    if (std::find(asset.ExternalFiles.begin(), asset.ExternalFiles.end(), relative) == asset.ExternalFiles.end())
        asset.ExternalFiles.push_back(relative);
    return true;
}

std::size_t findValueStart(std::string_view object, std::string_view key) {
    const std::string needle = "\"" + std::string(key) + "\"";
    const auto keyPos = object.find(needle);
    if (keyPos == std::string_view::npos) return std::string_view::npos;
    const auto colon = object.find(':', keyPos + needle.size());
    if (colon == std::string_view::npos) return std::string_view::npos;
    auto start = colon + 1;
    while (start < object.size() && std::isspace(static_cast<unsigned char>(object[start]))) ++start;
    return start;
}

Range balancedRange(std::string_view input, std::size_t start, char open, char close) {
    if (start >= input.size() || input[start] != open) return {std::string_view::npos, std::string_view::npos};
    std::size_t depth = 0;
    bool quoted = false;
    bool escaped = false;
    for (std::size_t index = start; index < input.size(); ++index) {
        const char c = input[index];
        if (quoted) {
            if (escaped) escaped = false;
            else if (c == '\\') escaped = true;
            else if (c == '"') quoted = false;
            continue;
        }
        if (c == '"') {
            quoted = true;
            continue;
        }
        if (c == open) ++depth;
        else if (c == close && --depth == 0) return {start, index + 1};
    }
    return {std::string_view::npos, std::string_view::npos};
}

std::string stringField(std::string_view object, std::string_view key, std::string fallback = {}) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos || start >= object.size() || object[start] != '"') return fallback;
    std::string result;
    bool escaped = false;
    for (std::size_t index = start + 1; index < object.size(); ++index) {
        const char c = object[index];
        if (escaped) {
            result.push_back(c == 'n' ? '\n' : c == 'r' ? '\r' : c == 't' ? '\t' : c);
            escaped = false;
        } else if (c == '\\') escaped = true;
        else if (c == '"') return result;
        else result.push_back(c);
    }
    return fallback;
}

double numberField(std::string_view object, std::string_view key, double fallback = 0.0) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) return fallback;
    std::size_t end = start;
    while (end < object.size() && std::string_view("+-0123456789.eE").find(object[end]) != std::string_view::npos) ++end;
    try { return std::stod(std::string(object.substr(start, end - start))); }
    catch (...) { return fallback; }
}

std::int32_t intField(std::string_view object, std::string_view key, std::int32_t fallback = -1) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) return fallback;
    std::int32_t value = fallback;
    const auto result = std::from_chars(object.data() + start, object.data() + object.size(), value);
    return result.ec == std::errc{} ? value : fallback;
}

std::int32_t nestedIntField(std::string_view object, std::string_view key, std::string_view nestedKey, std::int32_t fallback = -1) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos || start >= object.size() || object[start] != '{') return fallback;
    const auto range = balancedRange(object, start, '{', '}');
    if (range.first == std::string_view::npos) return fallback;
    return intField(object.substr(range.first, range.second - range.first), nestedKey, fallback);
}

bool boolField(std::string_view object, std::string_view key, bool fallback = false) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) return fallback;
    if (object.substr(start, 4) == "true") return true;
    if (object.substr(start, 5) == "false") return false;
    return fallback;
}

std::vector<double> numberArray(std::string_view object, std::string_view key) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) return {};
    const auto range = balancedRange(object, start, '[', ']');
    if (range.first == std::string_view::npos) return {};
    std::vector<double> values;
    std::size_t cursor = range.first + 1;
    while (cursor < range.second - 1) {
        while (cursor < range.second && (std::isspace(static_cast<unsigned char>(object[cursor])) || object[cursor] == ',')) ++cursor;
        if (cursor >= range.second - 1) break;
        std::size_t end = cursor;
        while (end < range.second && std::string_view("+-0123456789.eE").find(object[end]) != std::string_view::npos) ++end;
        try { values.push_back(std::stod(std::string(object.substr(cursor, end - cursor)))); }
        catch (...) { break; }
        cursor = end;
    }
    return values;
}

std::vector<std::string_view> objectArray(std::string_view object, std::string_view key) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) return {};
    const auto range = balancedRange(object, start, '[', ']');
    if (range.first == std::string_view::npos) return {};
    std::vector<std::string_view> result;
    for (std::size_t cursor = range.first + 1; cursor < range.second - 1;) {
        while (cursor < range.second && (std::isspace(static_cast<unsigned char>(object[cursor])) || object[cursor] == ',')) ++cursor;
        if (cursor >= range.second - 1) break;
        if (object[cursor] != '{') {
            while (cursor < range.second && object[cursor] != ',') ++cursor;
            continue;
        }
        const auto item = balancedRange(object, cursor, '{', '}');
        if (item.first == std::string_view::npos || item.second > range.second) break;
        result.push_back(object.substr(item.first, item.second - item.first));
        cursor = item.second;
    }
    return result;
}

std::vector<std::int32_t> intArray(std::string_view object, std::string_view key) {
    std::vector<std::int32_t> result;
    for (const double value : numberArray(object, key)) result.push_back(static_cast<std::int32_t>(value));
    return result;
}

Vec3 vec3Field(std::string_view object, std::string_view key, Vec3 fallback) {
    const auto values = numberArray(object, key);
    return values.size() >= 3 ? Vec3{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2])} : fallback;
}

Quat quatField(std::string_view object, std::string_view key, Quat fallback) {
    const auto values = numberArray(object, key);
    return values.size() >= 4 ? Quat{static_cast<float>(values[0]), static_cast<float>(values[1]), static_cast<float>(values[2]), static_cast<float>(values[3])} : fallback;
}

std::vector<std::string_view> topLevelObjects(std::string_view input, std::string_view key) {
    const auto start = findValueStart(input, key);
    if (start == std::string_view::npos) return {};
    const auto range = balancedRange(input, start, '[', ']');
    if (range.first == std::string_view::npos) return {};
    std::vector<std::string_view> result;
    for (std::size_t cursor = range.first + 1; cursor < range.second - 1;) {
        while (cursor < range.second && (std::isspace(static_cast<unsigned char>(input[cursor])) || input[cursor] == ',')) ++cursor;
        if (cursor >= range.second - 1) break;
        if (input[cursor] != '{') { while (cursor < range.second && input[cursor] != ',') ++cursor; continue; }
        const auto item = balancedRange(input, cursor, '{', '}');
        if (item.first == std::string_view::npos || item.second > range.second) break;
        result.push_back(input.substr(item.first, item.second - item.first));
        cursor = item.second;
    }
    return result;
}

struct BufferPayload {
    std::vector<std::uint8_t> Bytes;
    std::size_t DeclaredByteLength = 0;
    bool Loaded = false;
};

struct BufferViewData {
    std::int32_t Buffer = -1;
    std::size_t ByteOffset = 0;
    std::size_t ByteLength = 0;
    std::size_t ByteStride = 0;
};

struct AccessorData {
    GltfAccessor Metadata;
    std::size_t ComponentCount = 0;
    std::size_t ComponentSize = 0;
    bool Sparse = false;
};

bool sizeField(std::string_view object, std::string_view key, std::size_t fallback, std::size_t& value) {
    const auto start = findValueStart(object, key);
    if (start == std::string_view::npos) {
        value = fallback;
        return true;
    }
    const auto result = std::from_chars(object.data() + start, object.data() + object.size(), value);
    if (result.ec != std::errc{} || result.ptr == object.data() + start) return false;
    return result.ptr == object.data() + object.size() || *result.ptr == ',' || *result.ptr == '}' ||
           *result.ptr == ']' || std::isspace(static_cast<unsigned char>(*result.ptr));
}

bool decodeBase64(std::string_view uri, std::vector<std::uint8_t>& bytes) {
    if (!uri.starts_with("data:")) return false;
    const auto comma = uri.find(',');
    if (comma == std::string_view::npos || uri.substr(0, comma).find(";base64") == std::string_view::npos) return false;

    std::array<std::int16_t, 256> table{};
    table.fill(-1);
    constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    for (std::size_t index = 0; index < alphabet.size(); ++index) table[static_cast<unsigned char>(alphabet[index])] = static_cast<std::int16_t>(index);

    std::uint32_t accumulator = 0;
    int bitCount = 0;
    bool paddingStarted = false;
    std::size_t paddingCount = 0;
    bytes.clear();
    for (const unsigned char character : uri.substr(comma + 1)) {
        if (std::isspace(character)) continue;
        if (character == '=') {
            paddingStarted = true;
            ++paddingCount;
            if (paddingCount > 2) return false;
            continue;
        }
        if (paddingStarted || table[character] < 0) return false;
        accumulator = (accumulator << 6u) | static_cast<std::uint32_t>(table[character]);
        bitCount += 6;
        if (bitCount >= 8) {
            bitCount -= 8;
            if (bytes.size() == MaxAssetFileBytes) return false;
            bytes.push_back(static_cast<std::uint8_t>((accumulator >> static_cast<unsigned>(bitCount)) & 0xffu));
            accumulator &= bitCount == 0 ? 0u : (1u << static_cast<unsigned>(bitCount)) - 1u;
        }
    }
    if (bitCount == 6 || (paddingCount == 1 && bitCount != 2) || (paddingCount == 2 && bitCount != 4)) return false;
    return true;
}

bool loadBufferPayloads(std::string_view json, const std::filesystem::path& sourceDirectory,
                        const std::filesystem::path& assetRoot, GltfAsset& asset,
                        std::vector<BufferPayload>& buffers, bool requirePayloads, std::string* error) {
    const auto bufferObjects = topLevelObjects(json, "buffers");
    buffers.clear();
    buffers.reserve(bufferObjects.size());
    std::size_t totalBytes = 0;
    for (const auto object : bufferObjects) {
        BufferPayload buffer;
        if (!sizeField(object, "byteLength", 0, buffer.DeclaredByteLength) || buffer.DeclaredByteLength > MaxAssetFileBytes) {
            if (error) *error = "glTF buffer has an invalid byteLength";
            return false;
        }
        if (buffer.DeclaredByteLength > MaxAssetFileBytes * 2 - totalBytes) {
            if (error) *error = "glTF buffers exceed the asset allocation limit";
            return false;
        }
        totalBytes += buffer.DeclaredByteLength;
        const std::string uri = stringField(object, "uri");
        if (!addExternalFile(asset, uri, sourceDirectory, assetRoot, error)) return false;
        if (uri.starts_with("data:")) {
            if (!decodeBase64(uri, buffer.Bytes)) {
                if (error) *error = "glTF buffer has an invalid or unsupported data URI";
                return false;
            }
            buffer.Loaded = true;
        } else if (!uri.empty() && !sourceDirectory.empty()) {
            std::filesystem::path relative, source;
            if (!decodeExternalUri(uri, relative, error) || !resolveExternalFile(sourceDirectory, assetRoot, relative, source, error)) return false;
            buffer.Loaded = readFileBytes(source, buffer.Bytes, requirePayloads ? error : nullptr);
        }
        if (buffer.Loaded && buffer.Bytes.size() < buffer.DeclaredByteLength) {
            if (error) *error = "glTF buffer payload is shorter than its declared byteLength";
            return false;
        }
        buffers.push_back(std::move(buffer));
    }
    if (requirePayloads && std::any_of(buffers.begin(), buffers.end(), [](const BufferPayload& buffer) { return !buffer.Loaded; })) {
        if (error) *error = "Required glTF buffer payload could not be loaded";
        return false;
    }
    return true;
}

bool loadImages(GltfAsset& asset, const std::filesystem::path& directory,
                 const std::filesystem::path& root, bool requirePayloads, std::string* error) {
    for (auto& image : asset.Images) {
        if (!addExternalFile(asset, image.Uri, directory, root, error)) return false;
        if (!image.Data.empty() || image.Uri.empty()) continue;
        if (image.Uri.starts_with("data:")) {
            if (!decodeBase64(image.Uri, image.Data)) {
                if (error) *error = "Invalid image data URI";
                return false;
            }
        } else if (!directory.empty()) {
            std::filesystem::path relative, source;
            if (!decodeExternalUri(image.Uri, relative, error) || !resolveExternalFile(directory, root, relative, source, error)) return false;
            if (!readFileBytes(source, image.Data, requirePayloads ? error : nullptr) && requirePayloads) return false;
        }
        if (requirePayloads && image.Data.empty()) {
            if (error) *error = "Required glTF image payload could not be loaded: " + image.Uri;
            return false;
        }
    }
    return true;
}

std::size_t componentSize(std::uint32_t componentType) {
    switch (componentType) {
    case 5120: case 5121: return 1;
    case 5122: case 5123: return 2;
    case 5125: case 5126: return 4;
    default: return 0;
    }
}

std::size_t componentCount(std::string_view type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4") return 4;
    if (type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    return 0;
}

std::uint32_t readU32LittleEndian(const std::uint8_t* data) {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8u) |
           (static_cast<std::uint32_t>(data[2]) << 16u) |
           (static_cast<std::uint32_t>(data[3]) << 24u);
}

bool accessorComponentAddress(const AccessorData& accessor, std::size_t element, std::size_t component,
                              const std::vector<BufferPayload>& buffers, const std::vector<BufferViewData>& views,
                              const std::uint8_t*& address, std::string* error) {
    if (accessor.Sparse) {
        if (error) *error = "Sparse glTF accessors are not supported by the dependency-free importer";
        return false;
    }
    if (element >= accessor.Metadata.Count || component >= accessor.ComponentCount) {
        if (error) *error = "glTF accessor element is out of range";
        return false;
    }
    if (accessor.Metadata.BufferView == -1) {
        address = nullptr;
        return true;
    }
    const auto viewIndex = static_cast<std::size_t>(accessor.Metadata.BufferView);
    if (viewIndex >= views.size()) {
        if (error) *error = "glTF accessor references an invalid bufferView";
        return false;
    }
    const BufferViewData& view = views[viewIndex];
    if (view.Buffer < 0 || static_cast<std::size_t>(view.Buffer) >= buffers.size() || !buffers[static_cast<std::size_t>(view.Buffer)].Loaded) {
        if (error) *error = "glTF accessor references an unavailable buffer payload";
        return false;
    }
    const auto& bytes = buffers[static_cast<std::size_t>(view.Buffer)].Bytes;
    if (view.ByteOffset > bytes.size() || view.ByteLength > bytes.size() - view.ByteOffset) {
        if (error) *error = "glTF bufferView exceeds its buffer payload";
        return false;
    }
    const std::size_t elementSize = accessor.ComponentCount * accessor.ComponentSize;
    const std::size_t stride = view.ByteStride == 0 ? elementSize : view.ByteStride;
    if (stride < elementSize || accessor.Metadata.ByteOffset > view.ByteLength || elementSize > view.ByteLength - accessor.Metadata.ByteOffset) {
        if (error) *error = "glTF accessor does not fit within its bufferView";
        return false;
    }
    if (element != 0 && stride > (std::numeric_limits<std::size_t>::max() - accessor.Metadata.ByteOffset) / element) {
        if (error) *error = "glTF accessor byte offset overflows";
        return false;
    }
    const std::size_t elementOffset = accessor.Metadata.ByteOffset + element * stride;
    if (component > std::numeric_limits<std::size_t>::max() / accessor.ComponentSize) {
        if (error) *error = "glTF accessor component offset overflows";
        return false;
    }
    const std::size_t componentOffset = component * accessor.ComponentSize;
    if (elementOffset > view.ByteLength || componentOffset > view.ByteLength - elementOffset ||
        accessor.ComponentSize > view.ByteLength - elementOffset - componentOffset) {
        if (error) *error = "glTF accessor reads beyond its bufferView";
        return false;
    }
    address = bytes.data() + view.ByteOffset + elementOffset + componentOffset;
    return true;
}

bool readAccessorFloat(const AccessorData& accessor, std::size_t element, std::size_t component,
                       const std::vector<BufferPayload>& buffers, const std::vector<BufferViewData>& views,
                       float& value, std::string* error) {
    const std::uint8_t* address = nullptr;
    if (!accessorComponentAddress(accessor, element, component, buffers, views, address, error)) return false;
    if (!address) { value = 0.0f; return true; }
    switch (accessor.Metadata.ComponentType) {
    case 5120: {
        const auto raw = std::bit_cast<std::int8_t>(*address);
        value = accessor.Metadata.Normalized ? std::max(static_cast<float>(raw) / 127.0f, -1.0f) : static_cast<float>(raw);
        return true;
    }
    case 5121:
        value = accessor.Metadata.Normalized ? static_cast<float>(*address) / 255.0f : static_cast<float>(*address);
        return true;
    case 5122: {
        const std::uint16_t encoded = static_cast<std::uint16_t>(address[0]) | (static_cast<std::uint16_t>(address[1]) << 8u);
        const auto raw = std::bit_cast<std::int16_t>(encoded);
        value = accessor.Metadata.Normalized ? std::max(static_cast<float>(raw) / 32767.0f, -1.0f) : static_cast<float>(raw);
        return true;
    }
    case 5123: {
        const std::uint16_t raw = static_cast<std::uint16_t>(address[0]) | (static_cast<std::uint16_t>(address[1]) << 8u);
        value = accessor.Metadata.Normalized ? static_cast<float>(raw) / 65535.0f : static_cast<float>(raw);
        return true;
    }
    case 5125:
        value = static_cast<float>(readU32LittleEndian(address));
        return true;
    case 5126:
        value = std::bit_cast<float>(readU32LittleEndian(address));
        if (!std::isfinite(value)) {
            if (error) *error = "glTF accessor contains a non-finite floating-point value";
            return false;
        }
        return true;
    default:
        if (error) *error = "glTF accessor uses an unsupported componentType";
        return false;
    }
}

bool readAccessorIndex(const AccessorData& accessor, std::size_t element,
                       const std::vector<BufferPayload>& buffers, const std::vector<BufferViewData>& views,
                       std::uint32_t& value, std::string* error) {
    const std::uint8_t* address = nullptr;
    if (!accessorComponentAddress(accessor, element, 0, buffers, views, address, error)) return false;
    if (!address) {
        if (error) *error = "glTF index accessor has no bufferView";
        return false;
    }
    switch (accessor.Metadata.ComponentType) {
    case 5121: value = address[0]; return true;
    case 5123: value = static_cast<std::uint32_t>(address[0]) | (static_cast<std::uint32_t>(address[1]) << 8u); return true;
    case 5125: value = readU32LittleEndian(address); return true;
    default:
        if (error) *error = "glTF index accessor must use UNSIGNED_BYTE, UNSIGNED_SHORT, or UNSIGNED_INT";
        return false;
    }
}

bool decodeMeshPayloads(GltfAsset& asset, const std::vector<AccessorData>& accessors,
                        const std::vector<BufferViewData>& views, const std::vector<BufferPayload>& buffers,
                        std::string* error) {
    std::size_t decodedBytes = 0;
    for (auto& mesh : asset.Meshes) {
        for (auto& primitive : mesh.Primitives) {
            const auto accessorFor = [&accessors](std::int32_t index) -> const AccessorData* {
                return index >= 0 && static_cast<std::size_t>(index) < accessors.size() ? &accessors[static_cast<std::size_t>(index)] : nullptr;
            };
            const AccessorData* positions = accessorFor(primitive.PositionAccessor);
            const AccessorData* normals = accessorFor(primitive.NormalAccessor);
            const AccessorData* tangents = accessorFor(primitive.TangentAccessor);
            const AccessorData* texcoords = accessorFor(primitive.Texcoord0Accessor);
            const AccessorData* indices = accessorFor(primitive.IndicesAccessor);
            if ((primitive.NormalAccessor != -1 && !normals) || (primitive.TangentAccessor != -1 && !tangents) ||
                (primitive.Texcoord0Accessor != -1 && !texcoords) || (primitive.IndicesAccessor != -1 && !indices)) {
                if (error) *error = "Mesh primitive references an invalid vertex or index accessor";
                return false;
            }
            if (!positions) {
                if (error) *error = "Mesh primitive references an invalid POSITION accessor";
                return false;
            }
            if (positions->Metadata.Type != "VEC3" || positions->Metadata.ComponentType != 5126) {
                if (error) *error = "Mesh POSITION accessor must be FLOAT VEC3";
                return false;
            }
            const auto validAttribute = [&](const AccessorData* accessor, std::string_view type, bool normalizedSigned) {
                if (!accessor) return true;
                if (accessor->Metadata.Type != type || accessor->Metadata.Count != positions->Metadata.Count) return false;
                if (accessor->Metadata.ComponentType == 5126) return true;
                if (!accessor->Metadata.Normalized) return false;
                if (normalizedSigned) return accessor->Metadata.ComponentType == 5120 || accessor->Metadata.ComponentType == 5122;
                return accessor->Metadata.ComponentType == 5121 || accessor->Metadata.ComponentType == 5123;
            };
            if (!validAttribute(normals, "VEC3", true) || !validAttribute(tangents, "VEC4", true) || !validAttribute(texcoords, "VEC2", false)) {
                if (error) *error = "Mesh vertex attribute accessor has an invalid type, componentType, or count";
                return false;
            }
            if (indices && (indices->Metadata.Type != "SCALAR" || indices->Metadata.Normalized ||
                            (indices->Metadata.ComponentType != 5121 && indices->Metadata.ComponentType != 5123 && indices->Metadata.ComponentType != 5125))) {
                if (error) *error = "Mesh index accessor has an invalid type or componentType";
                return false;
            }

            for (const AccessorData* accessor : {positions, normals, tangents, texcoords, indices}) {
                if (!accessor) continue;
                const std::uint8_t* lastComponent = nullptr;
                if (!accessorComponentAddress(*accessor, accessor->Metadata.Count - 1, accessor->ComponentCount - 1,
                                              buffers, views, lastComponent, error)) return false;
            }

            primitive.Vertices.clear();
            primitive.Indices.clear();
            if (!accountGeometry(positions->Metadata.Count, indices ? indices->Metadata.Count : positions->Metadata.Count,
                                 decodedBytes, error)) return false;
            primitive.Vertices.reserve(positions->Metadata.Count);
            for (std::size_t vertexIndex = 0; vertexIndex < positions->Metadata.Count; ++vertexIndex) {
                GltfVertex vertex;
                std::array<float, 4> values{};
                for (std::size_t component = 0; component < 3; ++component) {
                    if (!readAccessorFloat(*positions, vertexIndex, component, buffers, views, values[component], error)) return false;
                }
                vertex.Position = {values[0], values[1], values[2]};
                if (normals) {
                    for (std::size_t component = 0; component < 3; ++component) {
                        if (!readAccessorFloat(*normals, vertexIndex, component, buffers, views, values[component], error)) return false;
                    }
                    vertex.Normal = {values[0], values[1], values[2]};
                }
                if (tangents) {
                    for (std::size_t component = 0; component < 4; ++component) {
                        if (!readAccessorFloat(*tangents, vertexIndex, component, buffers, views, values[component], error)) return false;
                    }
                    vertex.Tangent = {values[0], values[1], values[2]};
                    vertex.TangentSign = values[3];
                }
                if (texcoords) {
                    for (std::size_t component = 0; component < 2; ++component) {
                        if (!readAccessorFloat(*texcoords, vertexIndex, component, buffers, views, values[component], error)) return false;
                    }
                    vertex.U = values[0];
                    vertex.V = values[1];
                }
                primitive.Vertices.push_back(vertex);
            }
            const std::size_t indexCount = indices ? indices->Metadata.Count : positions->Metadata.Count;
            primitive.Indices.reserve(indexCount);
            for (std::size_t index = 0; index < indexCount; ++index) {
                std::uint32_t vertexIndex = static_cast<std::uint32_t>(index);
                if (indices && !readAccessorIndex(*indices, index, buffers, views, vertexIndex, error)) return false;
                if (vertexIndex >= positions->Metadata.Count) {
                    if (error) *error = "Mesh index exceeds vertex count";
                    return false;
                }
                primitive.Indices.push_back(vertexIndex);
            }
            if (primitive.Mode == "TRIANGLES" && primitive.Indices.size() % 3 != 0) {
                if (error) *error = "Triangle index count must be divisible by three";
                return false;
            }
        }
    }
    return true;
}

#if defined(TINYENGINE_WITH_CGLTF)
struct CgltfFileContext { std::filesystem::path Root; };

cgltf_result boundedCgltfRead(const cgltf_memory_options* memory, const cgltf_file_options* fileOptions,
                              const char* path, cgltf_size* size, void** data) {
    const auto& context = *static_cast<CgltfFileContext*>(fileOptions->user_data);
    std::error_code error;
    const auto source = std::filesystem::canonical(path, error);
    if (error || !isWithinRoot(source, context.Root)) return cgltf_result_io_error;
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(source, bytes, nullptr)) return cgltf_result_io_error;
    const auto required = *size == 0 ? bytes.size() : *size;
    if (required > bytes.size()) return cgltf_result_data_too_short;
    void* allocation = memory->alloc_func ? memory->alloc_func(memory->user_data, std::max(required, std::size_t{1}))
        : std::malloc(std::max(required, std::size_t{1}));
    if (!allocation) return cgltf_result_out_of_memory;
    if (required != 0) std::memcpy(allocation, bytes.data(), required);
    *size = required;
    *data = allocation;
    return cgltf_result_success;
}

void boundedCgltfRelease(const cgltf_memory_options* memory, const cgltf_file_options*, void* data) {
    if (memory->free_func) memory->free_func(memory->user_data, data);
    else std::free(data);
}

bool validateCgltfRanges(const cgltf_data& data, std::string* error) {
    const auto reject = [error](const char* message) { if (error) *error = message; return false; };
    std::size_t totalBytes = 0;
    for (cgltf_size index = 0; index < data.buffers_count; ++index) {
        const auto size = data.buffers[index].size;
        if (size > MaxAssetFileBytes || size > MaxAssetFileBytes * 2 - totalBytes)
            return reject("glTF buffer allocation exceeds the asset limit");
        totalBytes += size;
    }
    for (cgltf_size index = 0; index < data.buffer_views_count; ++index) {
        const auto& view = data.buffer_views[index];
        if (!view.buffer || view.offset > view.buffer->size || view.size > view.buffer->size - view.offset)
            return reject("glTF bufferView exceeds its buffer");
        if (view.has_meshopt_compression) return reject("Meshopt compressed geometry is not supported");
    }
    for (cgltf_size index = 0; index < data.accessors_count; ++index) {
        const auto& accessor = data.accessors[index];
        const auto elementSize = cgltf_calc_size(accessor.type, accessor.component_type);
        if (accessor.is_sparse) return reject("Sparse glTF geometry is not supported");
        if (elementSize == 0 || accessor.count == 0 || accessor.count > MaxAssetFileBytes / sizeof(GltfVertex) ||
            accessor.offset > std::numeric_limits<std::uint32_t>::max()) return reject("Invalid or oversized glTF accessor");
        if (accessor.buffer_view) {
            const auto size = accessor.buffer_view->size;
            if (accessor.stride < elementSize || accessor.offset > size || elementSize > size - accessor.offset ||
                accessor.count - 1 > (size - accessor.offset - elementSize) / accessor.stride)
                return reject("glTF accessor exceeds its bufferView");
        }
    }
    std::size_t decodedBytes = 0;
    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        for (cgltf_size index = 0; index < data.meshes[meshIndex].primitives_count; ++index) {
            const auto& primitive = data.meshes[meshIndex].primitives[index];
            if (primitive.has_draco_mesh_compression)
                return reject("Draco compressed geometry is not supported");
            cgltf_size vertices = 0;
            for (cgltf_size attribute = 0; attribute < primitive.attributes_count; ++attribute) {
                if (primitive.attributes[attribute].type == cgltf_attribute_type_position && primitive.attributes[attribute].data)
                    vertices = primitive.attributes[attribute].data->count;
            }
            if (!accountGeometry(vertices, primitive.indices ? primitive.indices->count : vertices, decodedBytes, error)) return false;
        }
    }
    return true;
}

// cgltf's decoder trusts byteLength. Decode using the bounded parser first and
// keep the storage alive until import completes; cgltf does not own these bytes.
bool loadCgltfDataUris(cgltf_data& data, std::vector<std::vector<std::uint8_t>>& storage, std::string* error) {
    storage.resize(data.buffers_count);
    std::size_t totalBytes = 0;
    for (cgltf_size index = 0; index < data.buffers_count; ++index) {
        auto& buffer = data.buffers[index];
        if (!buffer.uri || !std::string_view(buffer.uri).starts_with("data:")) continue;
        auto& bytes = storage[index];
        if (!decodeBase64(buffer.uri, bytes) || bytes.size() < buffer.size || bytes.empty() ||
            bytes.size() > MaxAssetFileBytes * 2 - totalBytes) {
            if (error) *error = "Invalid, truncated, or oversized glTF buffer data URI";
            return false;
        }
        totalBytes += bytes.size();
        buffer.data = bytes.data();
        buffer.data_free_method = cgltf_data_free_method_none;
    }
    return true;
}

std::int32_t accessorIndex(const cgltf_data& data, const cgltf_accessor* accessor) {
    return accessor ? static_cast<std::int32_t>(accessor - data.accessors) : -1;
}

bool importCgltf(cgltf_data& data, const std::string& source, GltfAsset& asset, std::string* error) {
    GltfAsset imported;
    imported.SourcePath = source;
    imported.Generator = data.asset.generator ? data.asset.generator : "";
    for (cgltf_size index = 0; index < data.images_count; ++index) {
        const auto& image = data.images[index];
        imported.Images.push_back({image.name ? image.name : "", image.uri ? image.uri : "", image.mime_type ? image.mime_type : ""});
        if (image.buffer_view) {
            const auto* bytes = cgltf_buffer_view_data(image.buffer_view);
            if (!bytes) { if (error) *error = "Embedded image has no buffer data"; return false; }
            imported.Images.back().Data.assign(bytes, bytes + image.buffer_view->size);
        }
    }
    for (cgltf_size index = 0; index < data.textures_count; ++index) {
        const auto& texture = data.textures[index];
        imported.Textures.push_back({texture.image ? static_cast<std::int32_t>(texture.image - data.images) : -1, texture.name ? texture.name : ""});
    }
    const auto textureIndex = [&data](const cgltf_texture_view& view) { return view.texture ? static_cast<std::int32_t>(view.texture - data.textures) : -1; };
    for (cgltf_size index = 0; index < data.materials_count; ++index) {
        const auto& value = data.materials[index];
        PbrMaterial material;
        if (value.has_pbr_metallic_roughness) {
            const auto& pbr = value.pbr_metallic_roughness;
            material.BaseColor = {pbr.base_color_factor[0], pbr.base_color_factor[1], pbr.base_color_factor[2]};
            material.BaseAlpha = pbr.base_color_factor[3];
            material.Metallic = pbr.metallic_factor;
            material.Roughness = pbr.roughness_factor;
            material.BaseColorTexture = textureIndex(pbr.base_color_texture);
            material.MetallicRoughnessTexture = textureIndex(pbr.metallic_roughness_texture);
        }
        material.NormalTexture = textureIndex(value.normal_texture);
        material.NormalScale = value.normal_texture.scale;
        material.OcclusionTexture = textureIndex(value.occlusion_texture);
        material.OcclusionStrength = value.occlusion_texture.scale;
        material.EmissiveTexture = textureIndex(value.emissive_texture);
        material.Emissive = {value.emissive_factor[0], value.emissive_factor[1], value.emissive_factor[2]};
        material.AlphaCutoff = value.alpha_cutoff;
        material.Alpha = value.alpha_mode == cgltf_alpha_mode_mask ? PbrMaterial::AlphaMode::Mask :
            value.alpha_mode == cgltf_alpha_mode_blend ? PbrMaterial::AlphaMode::Blend : PbrMaterial::AlphaMode::Opaque;
        material.DoubleSided = value.double_sided != 0;
        if (!validatePbrMaterial(material, error)) return false;
        imported.Materials.push_back(material);
    }
    for (cgltf_size index = 0; index < data.accessors_count; ++index) {
        const auto& value = data.accessors[index];
        const char* type = value.type == cgltf_type_scalar ? "SCALAR" : value.type == cgltf_type_vec2 ? "VEC2" : value.type == cgltf_type_vec3 ? "VEC3" : value.type == cgltf_type_vec4 ? "VEC4" : "MATRIX";
        imported.Accessors.push_back({value.buffer_view ? static_cast<std::int32_t>(value.buffer_view - data.buffer_views) : -1, static_cast<std::uint32_t>(value.offset), static_cast<std::uint32_t>(value.count), type, static_cast<std::uint32_t>(value.component_type), value.normalized != 0});
    }
    for (cgltf_size meshIndex = 0; meshIndex < data.meshes_count; ++meshIndex) {
        const auto& mesh = data.meshes[meshIndex];
        GltfMesh result;
        result.Name = mesh.name ? mesh.name : "";
        for (cgltf_size primitiveIndex = 0; primitiveIndex < mesh.primitives_count; ++primitiveIndex) {
            const auto& value = mesh.primitives[primitiveIndex];
            if (value.type != cgltf_primitive_type_triangles) {
                if (error) *error = "Only triangle primitives are currently supported";
                return false;
            }
            GltfPrimitive primitive;
            const cgltf_accessor* positions = nullptr;
            const cgltf_accessor* normals = nullptr;
            const cgltf_accessor* tangents = nullptr;
            const cgltf_accessor* texcoords = nullptr;
            for (cgltf_size attributeIndex = 0; attributeIndex < value.attributes_count; ++attributeIndex) {
                const auto& attribute = value.attributes[attributeIndex];
                if (attribute.type == cgltf_attribute_type_position) positions = attribute.data;
                else if (attribute.type == cgltf_attribute_type_normal) normals = attribute.data;
                else if (attribute.type == cgltf_attribute_type_tangent) tangents = attribute.data;
                else if (attribute.type == cgltf_attribute_type_texcoord && attribute.index == 0) texcoords = attribute.data;
            }
            if (!positions) { if (error) *error = "Mesh primitive is missing POSITION data"; return false; }
            if (positions->type != cgltf_type_vec3 || positions->count == 0 ||
                (normals && (normals->type != cgltf_type_vec3 || normals->count != positions->count)) ||
                (tangents && (tangents->type != cgltf_type_vec4 || tangents->count != positions->count)) ||
                (texcoords && (texcoords->type != cgltf_type_vec2 || texcoords->count != positions->count))) {
                if (error) *error = "Mesh attributes must have compatible types and vertex counts";
                return false;
            }
            primitive.PositionAccessor = accessorIndex(data, positions);
            primitive.NormalAccessor = accessorIndex(data, normals);
            primitive.TangentAccessor = accessorIndex(data, tangents);
            primitive.Texcoord0Accessor = accessorIndex(data, texcoords);
            primitive.IndicesAccessor = accessorIndex(data, value.indices);
            primitive.MaterialIndex = value.material ? static_cast<std::int32_t>(value.material - data.materials) : -1;
            for (cgltf_size vertexIndex = 0; vertexIndex < positions->count; ++vertexIndex) {
                GltfVertex vertex;
                cgltf_float components[4]{};
                if (!cgltf_accessor_read_float(positions, vertexIndex, components, 3)) { if (error) *error = "Unable to decode mesh position"; return false; }
                vertex.Position = {components[0], components[1], components[2]};
                if (normals) {
                    if (!cgltf_accessor_read_float(normals, vertexIndex, components, 3)) { if (error) *error = "Unable to decode mesh normal"; return false; }
                    vertex.Normal = {components[0], components[1], components[2]};
                }
                if (tangents) {
                    if (!cgltf_accessor_read_float(tangents, vertexIndex, components, 4)) { if (error) *error = "Unable to decode mesh tangent"; return false; }
                    vertex.Tangent = {components[0], components[1], components[2]};
                    vertex.TangentSign = components[3];
                }
                if (texcoords) {
                    if (!cgltf_accessor_read_float(texcoords, vertexIndex, components, 2)) { if (error) *error = "Unable to decode mesh texture coordinates"; return false; }
                    vertex.U = components[0]; vertex.V = components[1];
                }
                const float values[] = {vertex.Position.x, vertex.Position.y, vertex.Position.z,
                    vertex.Normal.x, vertex.Normal.y, vertex.Normal.z, vertex.Tangent.x, vertex.Tangent.y,
                    vertex.Tangent.z, vertex.TangentSign, vertex.U, vertex.V};
                for (float number : values) if (!std::isfinite(number)) {
                    if (error) *error = "Mesh attributes contain non-finite data";
                    return false;
                }
                primitive.Vertices.push_back(vertex);
            }
            const cgltf_size indexCount = value.indices ? value.indices->count : positions->count;
            for (cgltf_size index = 0; index < indexCount; ++index) {
                const auto vertexIndex = value.indices ? cgltf_accessor_read_index(value.indices, index) : index;
                if (vertexIndex >= positions->count) { if (error) *error = "Mesh index exceeds vertex count"; return false; }
                primitive.Indices.push_back(static_cast<std::uint32_t>(vertexIndex));
            }
            if (primitive.Indices.size() % 3 != 0) { if (error) *error = "Triangle index count must be divisible by three"; return false; }
            result.Primitives.push_back(std::move(primitive));
        }
        imported.Meshes.push_back(std::move(result));
    }
    for (cgltf_size index = 0; index < data.nodes_count; ++index) {
        const auto& value = data.nodes[index];
        GltfNode node;
        node.Name = value.name ? value.name : "";
        node.MeshIndex = value.mesh ? static_cast<std::int32_t>(value.mesh - data.meshes) : -1;
        node.SkinIndex = value.skin ? static_cast<std::int32_t>(value.skin - data.skins) : -1;
        node.ParentIndex = value.parent ? static_cast<std::int32_t>(value.parent - data.nodes) : -1;
        node.Translation = {value.translation[0], value.translation[1], value.translation[2]};
        node.Rotation = {value.rotation[0], value.rotation[1], value.rotation[2], value.rotation[3]};
        node.Scale = {value.scale[0], value.scale[1], value.scale[2]};
        node.HasMatrix = value.has_matrix != 0;
        if (node.HasMatrix) std::copy_n(value.matrix, 16, node.Matrix.values.begin());
        for (cgltf_size child = 0; child < value.children_count; ++child) node.Children.push_back(static_cast<std::int32_t>(value.children[child] - data.nodes));
        imported.Nodes.push_back(std::move(node));
    }
    for (cgltf_size index = 0; index < data.scenes_count; ++index) {
        const auto& value = data.scenes[index];
        GltfScene scene;
        scene.Name = value.name ? value.name : "";
        for (cgltf_size nodeIndex = 0; nodeIndex < value.nodes_count; ++nodeIndex) scene.RootNodes.push_back(static_cast<std::int32_t>(value.nodes[nodeIndex] - data.nodes));
        imported.Scenes.push_back(std::move(scene));
    }
    imported.DefaultScene = data.scene ? static_cast<std::int32_t>(data.scene - data.scenes) : -1;
    asset = std::move(imported);
    return true;
}
#endif

bool loadJsonDocument(std::string_view json, GltfAsset& asset, std::string* error,
                      const GltfImportOptions& options, const std::filesystem::path& sourceDirectory);

} // namespace

bool GltfImporter::loadFromFile(const std::string& path, GltfAsset& asset, std::string* error, const GltfImportOptions& options) {
    std::error_code filesystemError;
    const auto source = std::filesystem::canonical(path, filesystemError);
    if (filesystemError) { if (error) *error = "Unable to resolve glTF file: " + path; return false; }
    const auto root = options.AssetRoot.empty() ? source.parent_path() : std::filesystem::canonical(options.AssetRoot, filesystemError);
    if (filesystemError || !isWithinRoot(source, root)) { if (error) *error = "glTF source is outside AssetRoot"; return false; }
#if defined(TINYENGINE_WITH_CGLTF)
    CgltfFileContext context{root};
    cgltf_options parserOptions{};
    parserOptions.file.read = &boundedCgltfRead;
    parserOptions.file.release = &boundedCgltfRelease;
    parserOptions.file.user_data = &context;
    cgltf_data* data = nullptr;
    const cgltf_result parsed = cgltf_parse_file(&parserOptions, path.c_str(), &data);
    if (parsed != cgltf_result_success) { if (error) *error = "cgltf parse failed (" + std::to_string(static_cast<int>(parsed)) + "): " + path; return false; }
    const auto freeData = std::unique_ptr<cgltf_data, decltype(&cgltf_free)>(data, &cgltf_free);
    if (!validateCgltfRanges(*data, error)) return false;
    std::vector<std::vector<std::uint8_t>> embeddedBuffers;
    if (!loadCgltfDataUris(*data, embeddedBuffers, error)) return false;
    GltfAsset imported;
    for (cgltf_size index = 0; index < data->buffers_count; ++index)
        if (data->buffers[index].uri && !addExternalFile(imported, data->buffers[index].uri, source.parent_path(), root, error)) return false;
    for (cgltf_size index = 0; index < data->images_count; ++index)
        if (data->images[index].uri && !addExternalFile(imported, data->images[index].uri, source.parent_path(), root, error)) return false;
    const cgltf_result loaded = cgltf_load_buffers(&parserOptions, data, path.c_str());
    if (loaded != cgltf_result_success) { if (error) *error = "cgltf buffer loading failed (" + std::to_string(static_cast<int>(loaded)) + ")"; return false; }
    if (cgltf_validate(data) != cgltf_result_success) { if (error) *error = "glTF validation failed"; return false; }
    auto externalFiles = std::move(imported.ExternalFiles);
    if (!importCgltf(*data, path, imported, error)) return false;
    imported.ExternalFiles = std::move(externalFiles);
    if (!loadImages(imported, source.parent_path(), root, options.RequireExternalPayloads, error)) return false;
    asset = std::move(imported);
    if (error) error->clear();
    return true;
#else
    std::vector<std::uint8_t> bytes;
    if (!readFileBytes(source, bytes, error)) return false;
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".glb") {
        if (error) *error = "GLB binary containers require the cgltf integration; use a .gltf JSON file with this importer";
        return false;
    }
    GltfImportOptions resolvedOptions = options;
    resolvedOptions.AssetRoot = root;
    const bool loaded = loadJsonDocument(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()),
        asset, error, resolvedOptions, source.parent_path());
    if (loaded) asset.SourcePath = path;
    return loaded;
#endif
}

namespace {

bool loadJsonDocument(std::string_view json, GltfAsset& asset, std::string* error,
                      const GltfImportOptions& options, const std::filesystem::path& sourceDirectory) {
    if (json.size() > MaxAssetFileBytes) { if (error) *error = "glTF document exceeds the 256 MiB limit"; return false; }
    GltfAsset imported;
    const auto assetStart = findValueStart(json, "asset");
    if (assetStart == std::string_view::npos) {
        if (error) *error = "Input is not a glTF 2.0 JSON document";
        return false;
    }
    const auto assetObject = balancedRange(json, assetStart, '{', '}');
    if (assetObject.first == std::string_view::npos || stringField(json.substr(assetObject.first, assetObject.second - assetObject.first), "version") != "2.0") {
        if (error) *error = "Input is not a glTF 2.0 JSON document";
        return false;
    }

    const auto assetRange = assetObject;
    if (assetRange.first != std::string_view::npos) imported.Generator = stringField(json.substr(assetRange.first, assetRange.second - assetRange.first), "generator");

    for (const auto object : topLevelObjects(json, "images")) imported.Images.push_back({stringField(object, "name"), stringField(object, "uri"), stringField(object, "mimeType")});
    for (const auto object : topLevelObjects(json, "textures")) imported.Textures.push_back({intField(object, "source"), stringField(object, "name")});

    for (const auto object : topLevelObjects(json, "materials")) {
        PbrMaterial material;
        const auto pbrStart = findValueStart(object, "pbrMetallicRoughness");
        std::string_view pbr = object;
        if (pbrStart != std::string_view::npos) {
            const auto range = balancedRange(object, pbrStart, '{', '}');
            if (range.first != std::string_view::npos) pbr = object.substr(range.first, range.second - range.first);
        }
        material.BaseColor = vec3Field(pbr, "baseColorFactor", material.BaseColor);
        const auto base = numberArray(pbr, "baseColorFactor");
        if (base.size() >= 3) material.BaseColor = {static_cast<float>(base[0]), static_cast<float>(base[1]), static_cast<float>(base[2])};
        material.Metallic = static_cast<float>(numberField(pbr, "metallicFactor", material.Metallic));
        material.Roughness = static_cast<float>(numberField(pbr, "roughnessFactor", material.Roughness));
        material.BaseColorTexture = nestedIntField(pbr, "baseColorTexture", "index");
        material.MetallicRoughnessTexture = nestedIntField(pbr, "metallicRoughnessTexture", "index");
        material.NormalTexture = nestedIntField(object, "normalTexture", "index");
        material.OcclusionTexture = nestedIntField(object, "occlusionTexture", "index");
        material.EmissiveTexture = nestedIntField(object, "emissiveTexture", "index");
        material.Emissive = vec3Field(object, "emissiveFactor", material.Emissive);
        material.AlphaCutoff = static_cast<float>(numberField(object, "alphaCutoff", material.AlphaCutoff));
        imported.Materials.push_back(material);
    }

    std::vector<BufferViewData> bufferViews;
    for (const auto object : topLevelObjects(json, "bufferViews")) {
        BufferViewData view;
        view.Buffer = intField(object, "buffer");
        if (!sizeField(object, "byteOffset", 0, view.ByteOffset) ||
            !sizeField(object, "byteLength", 0, view.ByteLength) ||
            !sizeField(object, "byteStride", 0, view.ByteStride)) {
            if (error) *error = "glTF bufferView contains an invalid byte range";
            return false;
        }
        bufferViews.push_back(view);
    }

    std::vector<AccessorData> accessors;
    for (const auto object : topLevelObjects(json, "accessors")) {
        AccessorData accessor;
        accessor.Metadata.BufferView = intField(object, "bufferView");
        std::size_t byteOffset = 0;
        std::size_t count = 0;
        std::size_t componentType = 0;
        if (!sizeField(object, "byteOffset", 0, byteOffset) || byteOffset > std::numeric_limits<std::uint32_t>::max() ||
            !sizeField(object, "count", 0, count) || count > MaxAssetFileBytes / sizeof(GltfVertex) ||
            !sizeField(object, "componentType", 0, componentType) || componentType > std::numeric_limits<std::uint32_t>::max()) {
            if (error) *error = "glTF accessor contains an invalid byte offset, count, or componentType";
            return false;
        }
        accessor.Metadata.ByteOffset = static_cast<std::uint32_t>(byteOffset);
        accessor.Metadata.Count = static_cast<std::uint32_t>(count);
        accessor.Metadata.Type = stringField(object, "type");
        accessor.Metadata.ComponentType = static_cast<std::uint32_t>(componentType);
        accessor.Metadata.Normalized = boolField(object, "normalized");
        accessor.ComponentCount = componentCount(accessor.Metadata.Type);
        accessor.ComponentSize = componentSize(accessor.Metadata.ComponentType);
        accessor.Sparse = findValueStart(object, "sparse") != std::string_view::npos;
        if (accessor.ComponentCount == 0 || accessor.ComponentSize == 0 || accessor.Metadata.Count == 0) {
            if (error) *error = "glTF accessor has an unsupported type or invalid component/count";
            return false;
        }
        imported.Accessors.push_back(accessor.Metadata);
        accessors.push_back(accessor);
    }

    std::vector<BufferPayload> buffers;
    if (!loadBufferPayloads(json, sourceDirectory, options.AssetRoot, imported, buffers, options.RequireExternalPayloads, error)) return false;
    if (!loadImages(imported, sourceDirectory, options.AssetRoot, options.RequireExternalPayloads, error)) return false;
    for (const auto object : topLevelObjects(json, "meshes")) {
        GltfMesh mesh;
        mesh.Name = stringField(object, "name");
        for (const auto primitive : objectArray(object, "primitives")) {
            GltfPrimitive value;
            const auto attrStart = findValueStart(primitive, "attributes");
            if (attrStart != std::string_view::npos) {
                const auto range = balancedRange(primitive, attrStart, '{', '}');
                if (range.first != std::string_view::npos) {
                    const auto attrs = primitive.substr(range.first, range.second - range.first);
                    value.PositionAccessor = intField(attrs, "POSITION");
                    value.NormalAccessor = intField(attrs, "NORMAL");
                    value.TangentAccessor = intField(attrs, "TANGENT");
                    value.Texcoord0Accessor = intField(attrs, "TEXCOORD_0");
                }
            }
            value.IndicesAccessor = intField(primitive, "indices");
            value.MaterialIndex = intField(primitive, "material");
            const auto mode = intField(primitive, "mode", 4);
            value.Mode = mode == 4 ? "TRIANGLES" : mode == 5 ? "TRIANGLE_STRIP" : mode == 6 ? "TRIANGLE_FAN" : "CUSTOM";
            const auto validAccessor = [&accessors](std::int32_t index) {
                return index >= -1 && (index == -1 || static_cast<std::size_t>(index) < accessors.size());
            };
            const bool validMaterial = value.MaterialIndex >= -1 &&
                (value.MaterialIndex == -1 || static_cast<std::size_t>(value.MaterialIndex) < imported.Materials.size());
            if (value.PositionAccessor < 0 || !validAccessor(value.PositionAccessor) ||
                !validAccessor(value.NormalAccessor) || !validAccessor(value.TangentAccessor) ||
                !validAccessor(value.Texcoord0Accessor) || !validAccessor(value.IndicesAccessor) || !validMaterial) {
                if (error) *error = "glTF mesh primitive references an invalid accessor or material";
                return false;
            }
            mesh.Primitives.push_back(value);
        }
        imported.Meshes.push_back(std::move(mesh));
    }

    for (const auto object : topLevelObjects(json, "nodes")) {
        GltfNode node;
        node.Name = stringField(object, "name");
        node.MeshIndex = intField(object, "mesh");
        node.SkinIndex = intField(object, "skin");
        node.Translation = vec3Field(object, "translation", node.Translation);
        node.Rotation = quatField(object, "rotation", node.Rotation);
        node.Scale = vec3Field(object, "scale", node.Scale);
        node.Children = intArray(object, "children");
        imported.Nodes.push_back(std::move(node));
    }
    for (std::size_t nodeIndex = 0; nodeIndex < imported.Nodes.size(); ++nodeIndex) for (const auto child : imported.Nodes[nodeIndex].Children) if (child >= 0 && static_cast<std::size_t>(child) < imported.Nodes.size()) imported.Nodes[static_cast<std::size_t>(child)].ParentIndex = static_cast<std::int32_t>(nodeIndex);
    for (const auto object : topLevelObjects(json, "scenes")) imported.Scenes.push_back({stringField(object, "name"), intArray(object, "nodes")});
    imported.DefaultScene = intField(json, "scene");

    const bool allBuffersLoaded = std::all_of(buffers.begin(), buffers.end(), [](const BufferPayload& buffer) { return buffer.Loaded; });
    if (allBuffersLoaded && !accessors.empty() && !imported.Meshes.empty() &&
        !decodeMeshPayloads(imported, accessors, bufferViews, buffers, error)) {
        return false;
    }
    asset = std::move(imported);
    if (error) error->clear();
    return true;
}

} // namespace

bool GltfImporter::loadFromString(std::string_view json, GltfAsset& asset, std::string* error, const GltfImportOptions& options) {
    return loadJsonDocument(json, asset, error, options, {});
}

} // namespace TinyEngine

#pragma once

#include <filesystem>
#include <cstddef>
#include <string>

namespace TinyEngine {

// Thread-safe. Returns the running executable's absolute path, or an empty
// path and an error if the platform cannot identify it. Does not use cwd/PATH.
std::filesystem::path GetExecutablePath(std::string* error = nullptr);

// Searches package/install locations beside executable, then this build's
// shader output. Returns empty when compiled shaders are unavailable.
std::filesystem::path FindShaderDirectory(const std::filesystem::path& executable);

// Thread-safe bounded binary text read. Rejects non-regular files, oversized
// input, and incomplete reads; destination is unchanged on failure.
bool ReadTextFile(const std::filesystem::path& path, std::size_t maxBytes,
    std::string& destination, std::string* error = nullptr);

} // namespace TinyEngine

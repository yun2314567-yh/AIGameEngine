#include "TinyEngine/Platform/Paths.h"

#include <array>
#include <fstream>
#include <limits>
#include <vector>
#if defined(_WIN32)
#define NOMINMAX
#include <windows.h>
#elif defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

namespace TinyEngine {

bool ReadTextFile(const std::filesystem::path& path, std::size_t maxBytes,
    std::string& destination, std::string* error) try {
    const auto fail = [error](const std::string& message) { if (error) *error = message; return false; };
    std::error_code filesystemError;
    if (!std::filesystem::is_regular_file(path, filesystemError)) return fail("Not a readable regular file: " + path.string());
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file) return fail("Could not open file: " + path.string());
    const auto length = file.tellg();
    if (length < 0 || static_cast<std::uintmax_t>(length) > maxBytes ||
        static_cast<std::uintmax_t>(length) > static_cast<std::uintmax_t>(std::numeric_limits<std::streamsize>::max()))
        return fail("File exceeds the configured size limit: " + path.string());
    std::string text(static_cast<std::size_t>(length), '\0');
    file.seekg(0);
    if (!file.read(text.data(), static_cast<std::streamsize>(text.size())) || file.peek() != std::char_traits<char>::eof())
        return fail("Could not read a consistent file: " + path.string());
    destination = std::move(text);
    if (error) error->clear();
    return true;
} catch (const std::exception& exception) {
    if (error) *error = std::string("File read failed: ") + exception.what();
    return false;
}

std::filesystem::path GetExecutablePath(std::string* error) {
    std::filesystem::path result;
#if defined(_WIN32)
    std::vector<wchar_t> buffer(32768);
    const auto length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length > 0 && length < buffer.size()) result = std::wstring(buffer.data(), length);
#elif defined(__APPLE__)
    std::uint32_t length = 0;
    _NSGetExecutablePath(nullptr, &length);
    std::vector<char> buffer(length);
    if (_NSGetExecutablePath(buffer.data(), &length) == 0) {
        std::error_code filesystemError;
        result = std::filesystem::canonical(buffer.data(), filesystemError);
        if (filesystemError) result.clear();
    }
#elif defined(__linux__)
    std::error_code filesystemError;
    result = std::filesystem::read_symlink("/proc/self/exe", filesystemError);
    if (filesystemError) result.clear();
#endif
    if (error) *error = result.empty() ? "Cannot determine the running executable path" : "";
    return result;
}

std::filesystem::path FindShaderDirectory(const std::filesystem::path& executable) {
    const std::array candidates{
        executable.parent_path() / "shaders",
        executable.parent_path().parent_path() / "share" / "TinyEngine" / "shaders",
#if defined(TINYENGINE_BUILD_SHADER_DIRECTORY)
        std::filesystem::path(TINYENGINE_BUILD_SHADER_DIRECTORY)
#else
        std::filesystem::path{}
#endif
    };
    for (const auto& directory : candidates) {
        std::error_code error;
        if (!directory.empty() && std::filesystem::is_regular_file(directory / "Pbr.vert.hlsl.spv", error)) return directory;
    }
    return {};
}

} // namespace TinyEngine

#include "TinyEngine/Renderer/Renderer.h"

#include <fstream>
#include <locale>
#include <set>
#include <sstream>

namespace TinyEngine {
namespace {
bool fail(std::string* error, const std::string& message) {
    if (error) *error = message;
    return false;
}
}

bool ApplyPbrMaterialOverride(std::string_view text, PbrMaterial& material, std::string* error) {
    if (error) error->clear();
    if (text.size() > 65536) return fail(error, "Material file exceeds 64 KiB");
    std::istringstream input{std::string(text)};
    input.imbue(std::locale::classic());
    std::string magic;
    unsigned version = 0;
    if (!(input >> magic >> version) || magic != "TINYENGINE_MATERIAL" || version != 1)
        return fail(error, "Expected TINYENGINE_MATERIAL 1");
    auto candidate = material;
    std::set<std::string> keys;
    std::string key;
    while (input >> key) {
        if (!keys.insert(key).second) return fail(error, "Duplicate material field: " + key);
        if (key == "base_color") input >> candidate.BaseColor.x >> candidate.BaseColor.y >> candidate.BaseColor.z >> candidate.BaseAlpha;
        else if (key == "metallic") input >> candidate.Metallic;
        else if (key == "roughness") input >> candidate.Roughness;
        else if (key == "normal_scale") input >> candidate.NormalScale;
        else if (key == "occlusion_strength") input >> candidate.OcclusionStrength;
        else if (key == "emissive") input >> candidate.Emissive.x >> candidate.Emissive.y >> candidate.Emissive.z;
        else if (key == "alpha_cutoff") input >> candidate.AlphaCutoff;
        else if (key == "alpha_mode") {
            std::string mode;
            input >> mode;
            if (mode == "opaque") candidate.Alpha = PbrMaterial::AlphaMode::Opaque;
            else if (mode == "mask") candidate.Alpha = PbrMaterial::AlphaMode::Mask;
            else if (mode == "blend") candidate.Alpha = PbrMaterial::AlphaMode::Blend;
            else return fail(error, "Unknown alpha mode: " + mode);
        } else if (key == "double_sided") {
            unsigned value = 0;
            input >> value;
            if (value > 1) return fail(error, "double_sided must be 0 or 1");
            candidate.DoubleSided = value != 0;
        } else return fail(error, "Unknown material field: " + key);
        if (!input) return fail(error, "Invalid value for material field: " + key);
    }
    if (!validatePbrMaterial(candidate, error)) return false;
    material = candidate;
    return true;
}

bool LoadPbrMaterialOverride(const std::filesystem::path& path, PbrMaterial& material, std::string* error) {
    std::ifstream input(path, std::ios::binary | std::ios::ate);
    if (!input) return fail(error, "Could not open material: " + path.string());
    const auto length = input.tellg();
    if (length < 0 || length > 65536) return fail(error, "Material file exceeds 64 KiB");
    std::string text(static_cast<std::size_t>(length), '\0');
    input.seekg(0);
    if (!input.read(text.data(), length)) return fail(error, "Could not read material: " + path.string());
    return ApplyPbrMaterialOverride(text, material, error);
}
} // namespace TinyEngine

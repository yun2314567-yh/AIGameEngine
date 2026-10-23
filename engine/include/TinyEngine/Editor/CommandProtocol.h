#pragma once

#include "TinyEngine/Scene.h"

#include <filesystem>
#include <iosfwd>
#include <utility>

namespace TinyEngine {

class EditorCommandProtocol {
public:
    explicit EditorCommandProtocol(Scene& scene, std::filesystem::path assetRoot = {})
        : m_Scene(scene), m_AssetRoot(std::move(assetRoot)) {}
    bool run(std::istream& input, std::ostream& output, std::ostream& errors);

private:
    Scene& m_Scene;
    std::filesystem::path m_AssetRoot;
};

} // namespace TinyEngine

#include <TinyEngine/Core.h>
#include <TinyEngine/Platform/Window.h>
#include <TinyEngine/Renderer/GraphicsContext.h>

#include <iostream>
#include <string>

int main() {
    TinyEngine::Engine engine;
    const auto entity = engine.scene().createEntity("Installed consumer");
    engine.scene().registry().emplace<TinyEngine::RigidBodyComponent>(entity);
    engine.update(1.0f / 60.0f);
    if (!engine.scene().valid(entity)) return 1;
    engine.Input().BeginFrame();
    if (!engine.Input().SetKey(TinyEngine::Key::Space, true) ||
        !engine.Input().WasKeyPressed(TinyEngine::Key::Space)) return 2;
    std::string error;
    if (TinyEngine::Window::create({"Invalid", 0, 0}, &error) || error.empty()) return 3;
    std::cout << "Installed TinyEngine runs; Vulkan compiled: "
              << TinyEngine::GraphicsContext::available() << '\n';
    return 0;
}

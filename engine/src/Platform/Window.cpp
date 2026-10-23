#include "TinyEngine/Platform/Window.h"

#include <algorithm>
#include <limits>

#if defined(TINYENGINE_WITH_GLFW)
#define GLFW_INCLUDE_NONE
#include <GLFW/glfw3.h>
#endif

namespace TinyEngine {

#if defined(TINYENGINE_WITH_GLFW)
namespace {

// GLFW requires calls on the main thread. Windows share this RAII token so
// closing one NO_API window cannot terminate GLFW beneath another window.
struct GlfwLibrary {
    ~GlfwLibrary() { glfwTerminate(); }
};

std::shared_ptr<GlfwLibrary> acquireGlfw() {
    static std::weak_ptr<GlfwLibrary> active;
    auto library = active.lock();
    if (!library && glfwInit() == GLFW_TRUE) {
        library = std::make_shared<GlfwLibrary>();
        active = library;
    }
    return library;
}

class GlfwWindow final : public Window {
public:
    explicit GlfwWindow(GLFWwindow* handle, std::shared_ptr<GlfwLibrary> library) : m_Handle(handle), m_Library(std::move(library)) {}
    ~GlfwWindow() override {
        if (m_Handle) glfwDestroyWindow(m_Handle);
    }

    void pollEvents() override { glfwPollEvents(); }
    // GLFW_NO_API windows have no OpenGL back buffer. A GPU backend owns
    // swapchain presentation; the platform window only pumps events.
    void present() override {}
    [[nodiscard]] bool shouldClose() const override { return glfwWindowShouldClose(m_Handle) == GLFW_TRUE; }
    [[nodiscard]] std::uint32_t width() const override {
        int value = 0;
        glfwGetFramebufferSize(m_Handle, &value, nullptr);
        return static_cast<std::uint32_t>(std::max(value, 0));
    }
    [[nodiscard]] std::uint32_t height() const override {
        int value = 0;
        glfwGetFramebufferSize(m_Handle, nullptr, &value);
        return static_cast<std::uint32_t>(std::max(value, 0));
    }
    [[nodiscard]] void* nativeHandle() const override { return m_Handle; }
    void CaptureInput(InputState& destination) const override {
        constexpr std::array keyCodes{
            GLFW_KEY_A, GLFW_KEY_B, GLFW_KEY_C, GLFW_KEY_D, GLFW_KEY_E, GLFW_KEY_F,
            GLFW_KEY_G, GLFW_KEY_H, GLFW_KEY_I, GLFW_KEY_J, GLFW_KEY_K, GLFW_KEY_L,
            GLFW_KEY_M, GLFW_KEY_N, GLFW_KEY_O, GLFW_KEY_P, GLFW_KEY_Q, GLFW_KEY_R,
            GLFW_KEY_S, GLFW_KEY_T, GLFW_KEY_U, GLFW_KEY_V, GLFW_KEY_W, GLFW_KEY_X,
            GLFW_KEY_Y, GLFW_KEY_Z, GLFW_KEY_0, GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
            GLFW_KEY_4, GLFW_KEY_5, GLFW_KEY_6, GLFW_KEY_7, GLFW_KEY_8, GLFW_KEY_9,
            GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_SPACE,
            GLFW_KEY_ENTER, GLFW_KEY_ESCAPE, GLFW_KEY_TAB, GLFW_KEY_BACKSPACE,
            GLFW_KEY_LEFT_SHIFT, GLFW_KEY_RIGHT_SHIFT, GLFW_KEY_LEFT_CONTROL, GLFW_KEY_RIGHT_CONTROL
        };
        static_assert(keyCodes.size() == static_cast<std::size_t>(Key::Count));
        destination.BeginFrame();
        if (glfwGetWindowAttrib(m_Handle, GLFW_FOCUSED) != GLFW_TRUE) {
            destination.ReleaseAll();
            return;
        }
        for (std::size_t index = 0; index < keyCodes.size(); ++index)
            destination.SetKey(static_cast<Key>(index), glfwGetKey(m_Handle, keyCodes[index]) == GLFW_PRESS);
    }

private:
    GLFWwindow* m_Handle = nullptr;
    std::shared_ptr<GlfwLibrary> m_Library;
};

} // namespace
#endif

std::unique_ptr<Window> Window::create(const WindowSpecification& specification, std::string* error) {
    if (specification.Width == 0 || specification.Height == 0 ||
        specification.Width > static_cast<std::uint32_t>(std::numeric_limits<int>::max()) ||
        specification.Height > static_cast<std::uint32_t>(std::numeric_limits<int>::max())) {
        if (error) *error = "Window dimensions must be greater than zero";
        return {};
    }
#if defined(TINYENGINE_WITH_GLFW)
    auto library = acquireGlfw();
    if (!library) {
        if (error) *error = "GLFW initialization failed";
        return {};
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    auto* handle = glfwCreateWindow(static_cast<int>(specification.Width), static_cast<int>(specification.Height), specification.Title.c_str(), nullptr, nullptr);
    if (!handle) {
        if (error) *error = "GLFW window creation failed";
        return {};
    }
    return std::make_unique<GlfwWindow>(handle, std::move(library));
#else
    (void)specification;
    if (error) *error = "TinyEngine was built without GLFW support";
    return {};
#endif
}

} // namespace TinyEngine

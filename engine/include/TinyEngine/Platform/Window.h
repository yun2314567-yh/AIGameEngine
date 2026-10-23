#pragma once

#include "TinyEngine/Input.h"

#include <cstdint>
#include <memory>
#include <string>

namespace TinyEngine {

struct WindowSpecification {
    std::string Title = "TinyEngine";
    std::uint32_t Width = 1280;
    std::uint32_t Height = 720;
    bool VSync = true;
};

class Window {
public:
    virtual ~Window() = default;
    virtual void pollEvents() = 0;
    virtual void present() = 0;
    [[nodiscard]] virtual bool shouldClose() const = 0;
    [[nodiscard]] virtual std::uint32_t width() const = 0;
    [[nodiscard]] virtual std::uint32_t height() const = 0;
    [[nodiscard]] virtual void* nativeHandle() const = 0;
    // Owning thread, after pollEvents. Starts a new input frame and releases
    // all keys while unfocused. Snapshot queries never consume key state.
    virtual void CaptureInput(InputState& destination) const = 0;

    // Returns null and fills error when the requested platform backend is unavailable.
    static std::unique_ptr<Window> create(const WindowSpecification& specification, std::string* error = nullptr);
};

} // namespace TinyEngine

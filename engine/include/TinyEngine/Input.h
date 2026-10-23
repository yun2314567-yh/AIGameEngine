#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace TinyEngine {

enum class Key : std::uint8_t {
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Digit0, Digit1, Digit2, Digit3, Digit4, Digit5, Digit6, Digit7, Digit8, Digit9,
    Left, Right, Up, Down, Space, Enter, Escape, Tab, Backspace,
    LeftShift, RightShift, LeftControl, RightControl, Count
};

// Keyboard snapshot owned by one Engine. All methods use the engine's owning
// thread. BeginFrame once before setting this frame's key states. Headless
// applications/tests may inject input through SetKey without a platform window.
class InputState {
public:
    void BeginFrame() { m_Previous = m_Current; }
    // Invalid enum values return false without changing state.
    bool SetKey(Key key, bool down);
    // Releases keys while retaining this frame's release edges (e.g. focus loss).
    void ReleaseAll() { m_Current.fill(false); }
    // Queries of invalid enum values return false.
    [[nodiscard]] bool IsKeyDown(Key key) const;
    [[nodiscard]] bool WasKeyPressed(Key key) const;
    [[nodiscard]] bool WasKeyReleased(Key key) const;
    // Stable, case-sensitive Lua names: A..Z, 0..9 and enum names for other keys.
    // Pure/thread-safe. Unknown names leave destination unchanged.
    [[nodiscard]] static bool KeyFromName(std::string_view name, Key& destination);

private:
    static constexpr std::size_t KeyCount = static_cast<std::size_t>(Key::Count);
    std::array<bool, KeyCount> m_Current{};
    std::array<bool, KeyCount> m_Previous{};
};

} // namespace TinyEngine

#include "TinyEngine/Input.h"

namespace TinyEngine {

bool InputState::SetKey(Key key, bool down) {
    const auto index = static_cast<std::size_t>(key);
    if (index >= KeyCount) return false;
    m_Current[index] = down;
    return true;
}

bool InputState::IsKeyDown(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < KeyCount && m_Current[index];
}

bool InputState::WasKeyPressed(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < KeyCount && m_Current[index] && !m_Previous[index];
}

bool InputState::WasKeyReleased(Key key) const {
    const auto index = static_cast<std::size_t>(key);
    return index < KeyCount && !m_Current[index] && m_Previous[index];
}

bool InputState::KeyFromName(std::string_view name, Key& destination) {
    constexpr std::array<std::string_view, KeyCount> names{
        "A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
        "N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
        "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
        "Left", "Right", "Up", "Down", "Space", "Enter", "Escape", "Tab", "Backspace",
        "LeftShift", "RightShift", "LeftControl", "RightControl"
    };
    for (std::size_t index = 0; index < names.size(); ++index) {
        if (name == names[index]) {
            destination = static_cast<Key>(index);
            return true;
        }
    }
    return false;
}

} // namespace TinyEngine

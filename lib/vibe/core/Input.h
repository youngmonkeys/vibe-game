#pragma once

#include <cstdint>

namespace vibe {

enum class Key : std::uint16_t {
    Unknown,
    A,
    B,
    C,
    D,
    E,
    F,
    G,
    H,
    I,
    J,
    K,
    L,
    M,
    N,
    O,
    P,
    Q,
    R,
    S,
    T,
    U,
    V,
    W,
    X,
    Y,
    Z,
    Num0,
    Num1,
    Num2,
    Num3,
    Num4,
    Num5,
    Num6,
    Num7,
    Num8,
    Num9,
    Return,
    Escape,
    Backspace,
    Tab,
    Space,
    Minus,
    Equals,
    LeftBracket,
    RightBracket,
    Backslash,
    Semicolon,
    Apostrophe,
    Grave,
    Comma,
    Period,
    Slash,
    F1,
    F2,
    F3,
    F4,
    F5,
    F6,
    F7,
    F8,
    F9,
    F10,
    F11,
    F12,
    Insert,
    Delete,
    Home,
    End,
    PageUp,
    PageDown,
    Left,
    Right,
    Up,
    Down,
    KeypadEnter,
    LeftShift,
    RightShift,
    LeftCtrl,
    RightCtrl,
    LeftAlt,
    RightAlt,
    LeftGui,
    RightGui,
    Count,
};

struct KeyMods {
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
    bool gui = false;
};

enum class MouseButton : std::uint8_t { None, Left, Middle, Right, X1, X2 };

enum class GamepadButton : std::uint8_t {
    Unknown,
    South,
    East,
    West,
    North,
    Back,
    Guide,
    Start,
    LeftStick,
    RightStick,
    LeftShoulder,
    RightShoulder,
    DpadUp,
    DpadDown,
    DpadLeft,
    DpadRight,
};

enum class GamepadAxis : std::uint8_t { LeftX, LeftY, RightX, RightY, LeftTrigger, RightTrigger };

enum class EventType : std::uint8_t {
    None,
    Quit,
    Terminating,
    WindowShown,
    WindowHidden,
    WindowPixelSizeChanged,
    KeyDown,
    KeyUp,
    TextInput,
    MouseButtonDown,
    MouseButtonUp,
    MouseMotion,
    MouseWheel,
    FingerDown,
    FingerUp,
    FingerMotion,
    FingerCanceled,
    GamepadAdded,
    GamepadRemoved,
    GamepadButtonDown,
    GamepadButtonUp,
};

struct InputEvent {
    EventType type = EventType::None;
    std::uint64_t timestampNs = 0;
    Key key = Key::Unknown;
    Key physicalKey = Key::Unknown;
    KeyMods mods;
    bool repeat = false;
    const char* text = nullptr;
    MouseButton button = MouseButton::None;
    bool leftHeld = false;
    float x = 0.0f;
    float y = 0.0f;
    float wheelX = 0.0f;
    float wheelY = 0.0f;
    std::uint64_t fingerId = 0;
    std::uint32_t deviceId = 0;
    GamepadButton gamepadButton = GamepadButton::Unknown;
    int width = 0;
    int height = 0;

    bool isKeyDown(Key k) const { return type == EventType::KeyDown && key == k; }
    bool isPhysicalKeyDown(Key k) const { return type == EventType::KeyDown && physicalKey == k; }
};

namespace input {

inline constexpr int kMaxGamepads = 4;

bool keyHeld(Key physicalKey);
bool gamepadOpen(int slot);
float gamepadAxis(int slot, GamepadAxis axis);
bool gamepadButtonHeld(int slot, GamepadButton button);
bool textInputActive();

} // namespace input

} // namespace vibe

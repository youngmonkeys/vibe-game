#include "vibe/core/Input.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>

#include <array>
#include <cstddef>

#include "vibe/sdl/Sdl.h"

namespace vibe {
namespace {

constexpr float kAxisMax = 32767.0f;

struct KeyCodes {
    SDL_Scancode scancode;
    SDL_Keycode keycode;
};

constexpr std::size_t kKeyCount = static_cast<std::size_t>(Key::Count);

constexpr std::array<KeyCodes, kKeyCount> kKeyCodes = {{
    {SDL_SCANCODE_UNKNOWN, SDLK_UNKNOWN},
    {SDL_SCANCODE_A, SDLK_A},
    {SDL_SCANCODE_B, SDLK_B},
    {SDL_SCANCODE_C, SDLK_C},
    {SDL_SCANCODE_D, SDLK_D},
    {SDL_SCANCODE_E, SDLK_E},
    {SDL_SCANCODE_F, SDLK_F},
    {SDL_SCANCODE_G, SDLK_G},
    {SDL_SCANCODE_H, SDLK_H},
    {SDL_SCANCODE_I, SDLK_I},
    {SDL_SCANCODE_J, SDLK_J},
    {SDL_SCANCODE_K, SDLK_K},
    {SDL_SCANCODE_L, SDLK_L},
    {SDL_SCANCODE_M, SDLK_M},
    {SDL_SCANCODE_N, SDLK_N},
    {SDL_SCANCODE_O, SDLK_O},
    {SDL_SCANCODE_P, SDLK_P},
    {SDL_SCANCODE_Q, SDLK_Q},
    {SDL_SCANCODE_R, SDLK_R},
    {SDL_SCANCODE_S, SDLK_S},
    {SDL_SCANCODE_T, SDLK_T},
    {SDL_SCANCODE_U, SDLK_U},
    {SDL_SCANCODE_V, SDLK_V},
    {SDL_SCANCODE_W, SDLK_W},
    {SDL_SCANCODE_X, SDLK_X},
    {SDL_SCANCODE_Y, SDLK_Y},
    {SDL_SCANCODE_Z, SDLK_Z},
    {SDL_SCANCODE_0, SDLK_0},
    {SDL_SCANCODE_1, SDLK_1},
    {SDL_SCANCODE_2, SDLK_2},
    {SDL_SCANCODE_3, SDLK_3},
    {SDL_SCANCODE_4, SDLK_4},
    {SDL_SCANCODE_5, SDLK_5},
    {SDL_SCANCODE_6, SDLK_6},
    {SDL_SCANCODE_7, SDLK_7},
    {SDL_SCANCODE_8, SDLK_8},
    {SDL_SCANCODE_9, SDLK_9},
    {SDL_SCANCODE_RETURN, SDLK_RETURN},
    {SDL_SCANCODE_ESCAPE, SDLK_ESCAPE},
    {SDL_SCANCODE_BACKSPACE, SDLK_BACKSPACE},
    {SDL_SCANCODE_TAB, SDLK_TAB},
    {SDL_SCANCODE_SPACE, SDLK_SPACE},
    {SDL_SCANCODE_MINUS, SDLK_MINUS},
    {SDL_SCANCODE_EQUALS, SDLK_EQUALS},
    {SDL_SCANCODE_LEFTBRACKET, SDLK_LEFTBRACKET},
    {SDL_SCANCODE_RIGHTBRACKET, SDLK_RIGHTBRACKET},
    {SDL_SCANCODE_BACKSLASH, SDLK_BACKSLASH},
    {SDL_SCANCODE_SEMICOLON, SDLK_SEMICOLON},
    {SDL_SCANCODE_APOSTROPHE, SDLK_APOSTROPHE},
    {SDL_SCANCODE_GRAVE, SDLK_GRAVE},
    {SDL_SCANCODE_COMMA, SDLK_COMMA},
    {SDL_SCANCODE_PERIOD, SDLK_PERIOD},
    {SDL_SCANCODE_SLASH, SDLK_SLASH},
    {SDL_SCANCODE_F1, SDLK_F1},
    {SDL_SCANCODE_F2, SDLK_F2},
    {SDL_SCANCODE_F3, SDLK_F3},
    {SDL_SCANCODE_F4, SDLK_F4},
    {SDL_SCANCODE_F5, SDLK_F5},
    {SDL_SCANCODE_F6, SDLK_F6},
    {SDL_SCANCODE_F7, SDLK_F7},
    {SDL_SCANCODE_F8, SDLK_F8},
    {SDL_SCANCODE_F9, SDLK_F9},
    {SDL_SCANCODE_F10, SDLK_F10},
    {SDL_SCANCODE_F11, SDLK_F11},
    {SDL_SCANCODE_F12, SDLK_F12},
    {SDL_SCANCODE_INSERT, SDLK_INSERT},
    {SDL_SCANCODE_DELETE, SDLK_DELETE},
    {SDL_SCANCODE_HOME, SDLK_HOME},
    {SDL_SCANCODE_END, SDLK_END},
    {SDL_SCANCODE_PAGEUP, SDLK_PAGEUP},
    {SDL_SCANCODE_PAGEDOWN, SDLK_PAGEDOWN},
    {SDL_SCANCODE_LEFT, SDLK_LEFT},
    {SDL_SCANCODE_RIGHT, SDLK_RIGHT},
    {SDL_SCANCODE_UP, SDLK_UP},
    {SDL_SCANCODE_DOWN, SDLK_DOWN},
    {SDL_SCANCODE_KP_ENTER, SDLK_KP_ENTER},
    {SDL_SCANCODE_LSHIFT, SDLK_LSHIFT},
    {SDL_SCANCODE_RSHIFT, SDLK_RSHIFT},
    {SDL_SCANCODE_LCTRL, SDLK_LCTRL},
    {SDL_SCANCODE_RCTRL, SDLK_RCTRL},
    {SDL_SCANCODE_LALT, SDLK_LALT},
    {SDL_SCANCODE_RALT, SDLK_RALT},
    {SDL_SCANCODE_LGUI, SDLK_LGUI},
    {SDL_SCANCODE_RGUI, SDLK_RGUI},
}};

Key fromScancode(SDL_Scancode scancode) {
    for (std::size_t i = 1; i < kKeyCount; ++i) {
        if (kKeyCodes[i].scancode == scancode) {
            return static_cast<Key>(i);
        }
    }
    return Key::Unknown;
}

Key fromKeycode(SDL_Keycode keycode) {
    for (std::size_t i = 1; i < kKeyCount; ++i) {
        if (kKeyCodes[i].keycode == keycode) {
            return static_cast<Key>(i);
        }
    }
    return Key::Unknown;
}

MouseButton fromMouseButton(Uint8 button) {
    return button >= SDL_BUTTON_LEFT && button <= SDL_BUTTON_X2 ? static_cast<MouseButton>(button) : MouseButton::None;
}

GamepadButton fromGamepadButton(Uint8 button) {
    return button <= SDL_GAMEPAD_BUTTON_DPAD_RIGHT ? static_cast<GamepadButton>(button + 1) : GamepadButton::Unknown;
}

std::array<SDL_Gamepad*, input::kMaxGamepads> gGamepads{};

SDL_Gamepad* gamepadAt(int slot) {
    return slot >= 0 && slot < input::kMaxGamepads ? gGamepads[static_cast<std::size_t>(slot)] : nullptr;
}

void readKey(const SDL_KeyboardEvent& key, InputEvent& out) {
    out.key = fromKeycode(key.key);
    out.physicalKey = fromScancode(key.scancode);
    out.repeat = key.repeat;
    out.deviceId = key.which;
    out.mods.shift = (key.mod & SDL_KMOD_SHIFT) != 0;
    out.mods.ctrl = (key.mod & SDL_KMOD_CTRL) != 0;
    out.mods.alt = (key.mod & SDL_KMOD_ALT) != 0;
    out.mods.gui = (key.mod & SDL_KMOD_GUI) != 0;
}

void readFinger(const SDL_TouchFingerEvent& finger, InputEvent& out) {
    out.fingerId = finger.fingerID;
    out.x = finger.x;
    out.y = finger.y;
}

} // namespace

namespace input {

bool keyHeld(Key physicalKey) {
    const auto index = static_cast<std::size_t>(physicalKey);
    if (index == 0 || index >= kKeyCount) {
        return false;
    }
    const bool* keys = SDL_GetKeyboardState(nullptr);
    return keys && keys[kKeyCodes[index].scancode];
}

bool gamepadOpen(int slot) {
    return gamepadAt(slot) != nullptr;
}

float gamepadAxis(int slot, GamepadAxis axis) {
    SDL_Gamepad* gamepad = gamepadAt(slot);
    if (!gamepad) {
        return 0.0f;
    }
    return static_cast<float>(SDL_GetGamepadAxis(gamepad, static_cast<SDL_GamepadAxis>(axis))) / kAxisMax;
}

bool gamepadButtonHeld(int slot, GamepadButton button) {
    SDL_Gamepad* gamepad = gamepadAt(slot);
    if (!gamepad || button == GamepadButton::Unknown) {
        return false;
    }
    return SDL_GetGamepadButton(gamepad, static_cast<SDL_GamepadButton>(static_cast<int>(button) - 1));
}

bool textInputActive() {
    SDL_Window* window = SDL_GetKeyboardFocus();
    return window && SDL_TextInputActive(window);
}

} // namespace input

namespace sdl {

bool toInputEvent(const SDL_Event& event, InputEvent& out) {
    out = InputEvent{};
    out.timestampNs = event.common.timestamp;
    switch (event.type) {
    case SDL_EVENT_QUIT:
        out.type = EventType::Quit;
        return true;
    case SDL_EVENT_TERMINATING:
        out.type = EventType::Terminating;
        return true;
    case SDL_EVENT_WINDOW_SHOWN:
        out.type = EventType::WindowShown;
        return true;
    case SDL_EVENT_WINDOW_HIDDEN:
        out.type = EventType::WindowHidden;
        return true;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        out.type = EventType::WindowPixelSizeChanged;
        out.width = event.window.data1;
        out.height = event.window.data2;
        return true;
    case SDL_EVENT_KEY_DOWN:
        out.type = EventType::KeyDown;
        readKey(event.key, out);
        return true;
    case SDL_EVENT_KEY_UP:
        out.type = EventType::KeyUp;
        readKey(event.key, out);
        return true;
    case SDL_EVENT_TEXT_INPUT:
        out.type = EventType::TextInput;
        out.text = event.text.text;
        return true;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        out.type = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseButtonDown : EventType::MouseButtonUp;
        out.button = fromMouseButton(event.button.button);
        out.x = event.button.x;
        out.y = event.button.y;
        return true;
    case SDL_EVENT_MOUSE_MOTION:
        out.type = EventType::MouseMotion;
        out.x = event.motion.x;
        out.y = event.motion.y;
        out.leftHeld = (event.motion.state & SDL_BUTTON_LMASK) != 0;
        return true;
    case SDL_EVENT_MOUSE_WHEEL:
        out.type = EventType::MouseWheel;
        out.x = event.wheel.mouse_x;
        out.y = event.wheel.mouse_y;
        out.wheelX = event.wheel.x;
        out.wheelY = event.wheel.y;
        return true;
    case SDL_EVENT_FINGER_DOWN:
        out.type = EventType::FingerDown;
        readFinger(event.tfinger, out);
        return true;
    case SDL_EVENT_FINGER_UP:
        out.type = EventType::FingerUp;
        readFinger(event.tfinger, out);
        return true;
    case SDL_EVENT_FINGER_MOTION:
        out.type = EventType::FingerMotion;
        readFinger(event.tfinger, out);
        return true;
    case SDL_EVENT_FINGER_CANCELED:
        out.type = EventType::FingerCanceled;
        readFinger(event.tfinger, out);
        return true;
    case SDL_EVENT_GAMEPAD_ADDED:
        out.type = EventType::GamepadAdded;
        out.deviceId = event.gdevice.which;
        return true;
    case SDL_EVENT_GAMEPAD_REMOVED:
        out.type = EventType::GamepadRemoved;
        out.deviceId = event.gdevice.which;
        return true;
    case SDL_EVENT_GAMEPAD_BUTTON_DOWN:
    case SDL_EVENT_GAMEPAD_BUTTON_UP:
        out.type =
            event.type == SDL_EVENT_GAMEPAD_BUTTON_DOWN ? EventType::GamepadButtonDown : EventType::GamepadButtonUp;
        out.deviceId = event.gbutton.which;
        out.gamepadButton = fromGamepadButton(event.gbutton.button);
        return true;
    default:
        return false;
    }
}

void onGamepadEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_JOYSTICK_ADDED:
        if (!SDL_IsGamepad(event.jdevice.which)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "joystick added without a gamepad mapping: %s (id %u), not used",
                        SDL_GetJoystickNameForID(event.jdevice.which), event.jdevice.which);
        }
        break;
    case SDL_EVENT_GAMEPAD_ADDED: {
        for (SDL_Gamepad* slot : gGamepads) {
            if (slot && SDL_GetGamepadID(slot) == event.gdevice.which) {
                return;
            }
        }
        for (SDL_Gamepad*& slot : gGamepads) {
            if (!slot) {
                slot = SDL_OpenGamepad(event.gdevice.which);
                if (!slot) {
                    SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_OpenGamepad(%u) failed: %s", event.gdevice.which,
                                 SDL_GetError());
                    return;
                }
                SDL_Log("gamepad added: %s (id %u)", SDL_GetGamepadName(slot), event.gdevice.which);
                return;
            }
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "gamepad added: id %u ignored, %d gamepads already open",
                    event.gdevice.which, input::kMaxGamepads);
        break;
    }
    case SDL_EVENT_GAMEPAD_REMOVED:
        for (SDL_Gamepad*& slot : gGamepads) {
            if (slot && SDL_GetGamepadID(slot) == event.gdevice.which) {
                SDL_CloseGamepad(slot);
                slot = nullptr;
                SDL_Log("gamepad removed: id %u", event.gdevice.which);
            }
        }
        break;
    default:
        break;
    }
}

void closeGamepads() {
    for (SDL_Gamepad*& slot : gGamepads) {
        if (slot) {
            SDL_CloseGamepad(slot);
            slot = nullptr;
        }
    }
}

} // namespace sdl

} // namespace vibe

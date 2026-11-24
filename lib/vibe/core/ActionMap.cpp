#include "vibe/core/ActionMap.h"

#include <algorithm>

namespace vibe::input {

void ActionMap::unbindId(int action) {
    bindings_.erase(std::remove_if(bindings_.begin(), bindings_.end(),
                                   [action](const Binding& binding) { return binding.action == action; }),
                    bindings_.end());
}

bool ActionMap::heldId(int action) const {
    for (const Binding& binding : bindings_) {
        if (binding.action != action) {
            continue;
        }
        switch (binding.source) {
        case Source::Key:
            if (keyHeld(binding.key)) {
                return true;
            }
            break;
        case Source::Button:
            for (int slot = 0; slot < kMaxGamepads; ++slot) {
                if (gamepadButtonHeld(slot, binding.button)) {
                    return true;
                }
            }
            break;
        case Source::Axis:
            for (int slot = 0; slot < kMaxGamepads; ++slot) {
                if (gamepadOpen(slot) && gamepadAxis(slot, binding.axis) * binding.direction >= binding.threshold) {
                    return true;
                }
            }
            break;
        }
    }
    return false;
}

bool ActionMap::pressedId(int action, const InputEvent& event) const {
    const bool keyDown = event.type == EventType::KeyDown && !event.repeat;
    const bool buttonDown = event.type == EventType::GamepadButtonDown;
    if (!keyDown && !buttonDown) {
        return false;
    }
    for (const Binding& binding : bindings_) {
        if (binding.action != action) {
            continue;
        }
        if (keyDown && binding.source == Source::Key && binding.key == event.physicalKey) {
            return true;
        }
        if (buttonDown && binding.source == Source::Button && binding.button == event.gamepadButton) {
            return true;
        }
    }
    return false;
}

} // namespace vibe::input

#pragma once

#include <vector>

#include "vibe/core/Input.h"

namespace vibe::input {

class ActionMap {
public:
    static constexpr float kStickThreshold = 0.5f;

    template <class Action>
    ActionMap& bindKey(Action action, Key physicalKey) {
        bindings_.push_back(Binding{id(action), Source::Key, physicalKey});
        return *this;
    }

    template <class Action>
    ActionMap& bindButton(Action action, GamepadButton button) {
        Binding binding{id(action), Source::Button};
        binding.button = button;
        bindings_.push_back(binding);
        return *this;
    }

    template <class Action>
    ActionMap& bindAxis(Action action, GamepadAxis axis, float direction, float threshold = kStickThreshold) {
        Binding binding{id(action), Source::Axis};
        binding.axis = axis;
        binding.direction = direction < 0.0f ? -1.0f : 1.0f;
        binding.threshold = threshold;
        bindings_.push_back(binding);
        return *this;
    }

    template <class Action>
    void unbind(Action action) {
        unbindId(id(action));
    }

    void clear() { bindings_.clear(); }

    template <class Action>
    bool held(Action action) const {
        return heldId(id(action));
    }

    template <class Action>
    bool pressed(Action action, const InputEvent& event) const {
        return pressedId(id(action), event);
    }

private:
    enum class Source { Key, Button, Axis };

    struct Binding {
        int action = 0;
        Source source = Source::Key;
        Key key = Key::Unknown;
        GamepadButton button = GamepadButton::Unknown;
        GamepadAxis axis = GamepadAxis::LeftX;
        float direction = 1.0f;
        float threshold = kStickThreshold;
    };

    template <class Action>
    static int id(Action action) {
        return static_cast<int>(action);
    }

    void unbindId(int action);
    bool heldId(int action) const;
    bool pressedId(int action, const InputEvent& event) const;

    std::vector<Binding> bindings_;
};

} // namespace vibe::input

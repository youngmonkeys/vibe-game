#include "vibe/platform/TouchText.h"

namespace vibe::platform {

void installTouchText() {}

void publishFields(const FieldRect*, int) {}

int takeFocus() {
    return -1;
}

void setValue(std::string_view, int) {}

bool takeValue(std::string&) {
    return false;
}

bool takeEnter() {
    return false;
}

bool takeBlur() {
    return false;
}

void blur() {}

bool active() {
    return false;
}

} // namespace vibe::platform

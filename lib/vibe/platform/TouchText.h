#pragma once

#include <string>
#include <string_view>

namespace vibe::platform {

// Plan 14: the on-screen keyboard. SDL 3.4.18's Emscripten backend never
// shows one (research/dependencies.md), and iOS opens the keyboard only for
// an element focused inside a touch handler, while SDL hands the app its
// events later, in SDL_AppIterate. So the page keeps a hidden text proxy
// (#text-proxy in platform/web/shell.html):
//
// 1. Each frame the UI publishes the rectangles of the text fields on
//    screen, in canvas pixels (publishFields).
// 2. A `touchend` on one of them focuses the proxy at once, inside the
//    touch, which opens the keyboard. takeFocus() then names that field, and
//    the UI focuses it and loads its text into the proxy (setValue).
// 3. Typing changes the proxy; takeValue() hands the whole new value to the
//    focused field, which keeps its own limits. Enter and leaving the proxy
//    come back through takeEnter() and takeBlur().
//
// Key events on the proxy stop there, so SDL's window listeners do not post
// them a second time. Desktop mouse clicks never reach any of this.
void installTouchText();

struct FieldRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    bool password = false;
};

// Up to kMaxFields rectangles; publishing the same list again costs nothing.
inline constexpr int kMaxFields = 4;
void publishFields(const FieldRect* fields, int count);

// The index (into the last published list) of the field a touch focused,
// or -1.
int takeFocus();
// Loads `text` into the proxy, so editing starts from the field's text.
// `maxChars` bounds the proxy as the field bounds itself.
void setValue(std::string_view text, int maxChars);
// The proxy's value after the user typed. False when it has not changed.
// Never log it: it can be a password.
bool takeValue(std::string& out);
bool takeEnter();
bool takeBlur();
// Hides the keyboard (the field lost focus in the game).
void blur();
// True while the proxy has the browser's focus.
bool active();

} // namespace vibe::platform

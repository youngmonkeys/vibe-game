#pragma once

#include <cstddef>

namespace vibe::platform {

// Plan 14: what the page knows about the device it runs on.

// True when the user agent looks like a phone or tablet: Android, iPhone,
// iPad, iPod, or "Mobile", or a Mac user agent with more than one touch point
// (an iPad in desktop mode). Read once; the answer does not change.
bool isMobileUserAgent();

// The safe area (notch, rounded corners, home indicator) in CSS pixels, read
// from `env(safe-area-inset-*)` on the shell's #safe-probe element. SDL
// 3.4.18's SDL_GetWindowSafeArea returns the whole window on Emscripten
// (research/dependencies.md). Read again after each resize and
// orientationchange, not on every call.
struct SafeInsets {
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
    float left = 0.0f;
};
SafeInsets safeAreaInsets();

// Bytes of wasm memory the page holds now (the heap buffer, which only grows).
std::size_t wasmMemoryBytes();

std::size_t heapInUseBytes();

} // namespace vibe::platform

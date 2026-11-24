#pragma once

namespace vibe::platform {

// The fullscreen button (plan 14 follow-up, 2026-10-25). A page may go
// fullscreen only inside a user gesture's handler, and SDL hands the app its
// events later, in SDL_AppIterate, so the page does it, as with the
// on-screen keyboard (TouchText.h):
//
// 1. Each frame the UI publishes the button's rectangle, in canvas pixels,
//    or none (publishButton).
// 2. A tap (`touchend`) or a mouse press (`pointerdown`) on it toggles
//    fullscreen at once, inside the gesture. The whole page goes fullscreen,
//    not the canvas, so the text proxy and the safe-area probe stay in it
//    and SDL only sees a resize.
// 3. The same press also reaches the game as a click, which shows a tip
//    where the page cannot go fullscreen (iPhone: research/gotchas.md, Web).
//
// After entering, the page asks for a landscape lock; browsers allow one
// only in fullscreen, and the rotate screen still covers portrait without it.
void installFullscreen();

// The browser can make the page fullscreen. False on iPhone, whose browsers
// have no element fullscreen.
bool fullscreenSupported();
bool isFullscreen();
// Started from the home screen (a manifest `display` or iOS's
// apple-mobile-web-app-capable): there is no browser UI to hide.
bool runningAsApp();
// An iPhone or iPod, for the tip: there the way to full screen is Add to
// Home Screen.
bool isIphone();

// The button's rectangle this frame, in canvas pixels; w <= 0 for none.
// Publishing the same rectangle again costs nothing.
void publishButton(float x, float y, float w, float h);

} // namespace vibe::platform

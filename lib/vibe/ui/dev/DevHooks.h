#pragma once

// Dev builds only: CMake leaves src/ui/dev/ out of Release.

#include "vibe/render/Renderer.h"

namespace vibe::ui::dev {

// Remembers where a widget was drawn this frame, so the dev page test can
// click it through real mouse events:
//   Module.ccall('vibe_dev_ui_rect', 'string', ['string'], ['login.submit'])
// answers "x y w h" in canvas pixels, or "" when it was not drawn in the
// last frame.
void markRect(const char* id, const ScreenRect& rect);
// Once per frame, before the screens draw.
void beginFrame();

} // namespace vibe::ui::dev

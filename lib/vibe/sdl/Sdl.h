#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_video.h>

#include "vibe/core/Input.h"
#include "vibe/render/Renderer.h"

namespace vibe {

struct Renderer::Backend {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
};

namespace sdl {

inline SDL_Window* window(const Renderer& renderer) { return renderer.backend().window; }
inline SDL_Renderer* renderer(const Renderer& renderer) { return renderer.backend().renderer; }
inline SDL_Texture* texture(void* handle) { return static_cast<SDL_Texture*>(handle); }

bool toInputEvent(const SDL_Event& event, InputEvent& out);
void onGamepadEvent(const SDL_Event& event);
void closeGamepads();

} // namespace sdl

} // namespace vibe

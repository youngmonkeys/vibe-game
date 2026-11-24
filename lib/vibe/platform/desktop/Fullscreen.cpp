#include "vibe/platform/Fullscreen.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_video.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

namespace vibe::platform {
namespace {

bool gInstalled = false;
float gButton[4] = {0.0f, 0.0f, 0.0f, 0.0f};

#if !TARGET_OS_IOS && !defined(__ANDROID__)
SDL_Window* mainWindow() {
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* window = (windows && count > 0) ? windows[0] : nullptr;
    SDL_free(windows);
    return window;
}
#endif

bool SDLCALL onEvent(void*, SDL_Event* event) {
    if (event->type != SDL_EVENT_MOUSE_BUTTON_DOWN || event->button.button != SDL_BUTTON_LEFT || gButton[2] <= 0.0f) {
        return true;
    }
    SDL_Window* window = SDL_GetWindowFromID(event->button.windowID);
    if (!window) {
        return true;
    }
    const float density = SDL_GetWindowPixelDensity(window);
    const float x = event->button.x * density;
    const float y = event->button.y * density;
    if (x < gButton[0] || y < gButton[1] || x >= gButton[0] + gButton[2] || y >= gButton[1] + gButton[3]) {
        return true;
    }
    const bool on = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    if (!SDL_SetWindowFullscreen(window, !on)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "fullscreen: SDL_SetWindowFullscreen failed: %s", SDL_GetError());
    }
    return true;
}

} // namespace

void installFullscreen() {
    if (gInstalled) {
        return;
    }
    gInstalled = SDL_AddEventWatch(onEvent, nullptr);
}

bool fullscreenSupported() {
#if TARGET_OS_IOS || defined(__ANDROID__)
    return false;
#else
    return true;
#endif
}

bool isFullscreen() {
#if TARGET_OS_IOS || defined(__ANDROID__)
    return true;
#else
    SDL_Window* window = mainWindow();
    return window && (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
#endif
}

bool runningAsApp() {
#if TARGET_OS_IOS || defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

bool isIphone() {
    return false;
}

void publishButton(float x, float y, float w, float h) {
    if (w <= 0.0f) {
        x = y = w = h = 0.0f;
    }
    gButton[0] = x;
    gButton[1] = y;
    gButton[2] = w;
    gButton[3] = h;
}

} // namespace vibe::platform

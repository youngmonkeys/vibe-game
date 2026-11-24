// SDL main callbacks only. The browser calls SDL_AppIterate once per
// requestAnimationFrame; there is no loop here (docs/architecture.md, Frame).
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include "vibe/sdl/App.h"

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    auto* app = new vibe::App();
    // Set before init can fail: SDL calls SDL_AppQuit with it either way.
    *appstate = app;
    return app->init(argc, argv);
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    return static_cast<vibe::App*>(appstate)->onEvent(*event);
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    return static_cast<vibe::App*>(appstate)->iterate();
}

void SDL_AppQuit(void* appstate, SDL_AppResult result) {
    auto* app = static_cast<vibe::App*>(appstate);
    if (app) {
        app->quit(result);
        delete app;
    }
}

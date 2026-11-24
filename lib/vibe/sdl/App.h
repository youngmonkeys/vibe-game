#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>

#include <cstdint>
#include <memory>

#include "vibe/core/FixedStepClock.h"
#include "vibe/core/Game.h"
#include "vibe/core/HostConfig.h"
#include "vibe/core/Scene.h"
#include "vibe/core/SceneStack.h"
#include "vibe/render/Renderer.h"

#ifdef VIBE_DEV
#include "vibe/sdl/debug/DebugOverlay.h"
#endif

namespace vibe {

// Owns the window, the current scene, and the frame loop. main.cpp forwards
// each SDL main callback to one method here.
class App {
public:
    SDL_AppResult init(int argc, char* argv[]);
    SDL_AppResult onEvent(const SDL_Event& event);
    SDL_AppResult iterate();
    void quit(SDL_AppResult result);

private:
    void resetLoopStats(std::uint64_t nowNs);
    void recordLoopStats(std::uint64_t nowNs, int steps);

    Renderer renderer_;
    std::unique_ptr<Game> game_;
    SceneStack scenes_;
    HostConfig config_;
    FixedStepClock clock_;
#ifdef VIBE_DEV
    // Dear ImGui (plan 07). Dev builds only.
    debug::DebugOverlay overlay_;
#endif

    // Between SDL_EVENT_WINDOW_HIDDEN and SDL_EVENT_WINDOW_SHOWN.
    bool hidden_ = false;
    std::uint64_t hiddenAtNs_ = 0;

    // Once-a-second debug line that makes the loop visible in the console.
    std::uint64_t statsStartNs_ = 0;
    int statsFrames_ = 0;
    int statsSteps_ = 0;
    int statsMaxStepsPerFrame_ = 0;
};

} // namespace vibe

#include "vibe/sdl/App.h"

#include <SDL3/SDL.h>

#if defined(_WIN32)
#include <windows.h>
#elif !defined(__EMSCRIPTEN__)
#include <unistd.h>
#endif

#include <algorithm>
#include <cstring>
#include <string>

#include "vibe/core/AppInfo.h"
#include "vibe/core/Log.h"
#include "vibe/audio/Audio.h"
#include "vibe/platform/Device.h"
#include "vibe/sdl/Sdl.h"

namespace vibe {
namespace {

constexpr std::uint64_t kStatsIntervalNs = 1'000'000'000;

#if defined(_WIN32)
bool enterDirectory(const char* path) {
    const int length = MultiByteToWideChar(CP_UTF8, 0, path, -1, nullptr, 0);
    if (length <= 0) {
        return false;
    }
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, path, -1, wide.data(), length);
    return SetCurrentDirectoryW(wide.c_str()) != 0;
}
#endif

SDL_AppResult toSdl(AppResult result) {
    switch (result) {
    case AppResult::Success: return SDL_APP_SUCCESS;
    case AppResult::Failure: return SDL_APP_FAILURE;
    default: return SDL_APP_CONTINUE;
    }
}

} // namespace

bool GameContext::hasArgument(const char* name) const {
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], name) == 0) {
            return true;
        }
    }
    return false;
}

SDL_AppResult App::init(int argc, char* argv[]) {
    installLogOutput();
    game_ = createGame();
    if (!game_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "createGame returned no game");
        return SDL_APP_FAILURE;
    }

    const int version = SDL_GetVersion();
    SDL_Log("SDL %d.%d.%d (%s)", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
            SDL_VERSIONNUM_MICRO(version), SDL_GetRevision());

    // "/" on Emscripten; the preload package mounts config/ and assets/ under it.
    const char* basePath = SDL_GetBasePath();
    if (!basePath) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_GetBasePath failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    SDL_Log("base path: %s", basePath);
#if defined(_WIN32)
    const bool entered = enterDirectory(basePath);
#elif !defined(__EMSCRIPTEN__)
    const bool entered = chdir(basePath) == 0;
#else
    const bool entered = true;
#endif
    if (!entered) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot enter the base path %s", basePath);
        return SDL_APP_FAILURE;
    }

    const std::string configPath = std::string(basePath) + "config/configs.json";
    std::string error;
    if (!loadHostConfig(configPath.c_str(), config_, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "config: %s", error.c_str());
        return SDL_APP_FAILURE;
    }
    SDL_Log("config: zone %s, app %s", config_.zoneName.c_str(), config_.appName.c_str());
#ifndef VIBE_DEV
    warnInsecureUrls(config_);
#endif

    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    if (!renderer_.create(app_info::kName, 1280, 720)) {
        return SDL_APP_FAILURE;
    }
    // Plan 10. Not fatal: without a device the game runs silent.
    audio::init((std::string(basePath) + "assets/").c_str(), game_->audioCues());

#ifdef VIBE_DEV
    // A failed overlay is not fatal; the lobby then draws nothing.
    overlay_.init(sdl::window(renderer_), sdl::renderer(renderer_));
#endif
    const GameContext context{renderer_, config_, argc, argv};
    scene_ = game_->createInitialScene(context);
    if (!scene_) {
        scene_ = std::make_unique<EmptyScene>();
    }
    scene_->onEnter();
    game_->onStart(context);

    const std::uint64_t now = SDL_GetTicksNS();
    clock_.restart(now);
    resetLoopStats(now);
    return SDL_APP_CONTINUE;
}

SDL_AppResult App::onEvent(const SDL_Event& event) {
#ifdef VIBE_DEV
    // ImGui sees every event first. A key or click it owns (a focused text
    // field, a click on a window) does not reach the scene.
    const bool overlayOwns = overlay_.processEvent(event);
#else
    const bool overlayOwns = false;
#endif
    sdl::onGamepadEvent(event);
    switch (event.type) {
    case SDL_EVENT_QUIT:
        return SDL_APP_SUCCESS;
#ifdef VIBE_DEV
    case SDL_EVENT_KEY_DOWN:
        // Dev builds only, to test the clean-stop path. In Release, Escape
        // does nothing: a browser game has no quit, and Escape leaves fullscreen.
        // Not while an ImGui text field has the keyboard.
        if (event.key.key == SDLK_ESCAPE && !event.key.repeat && !overlayOwns) {
            SDL_Log("Escape: stopping (dev build)");
            return SDL_APP_SUCCESS;
        }
        break;
#endif
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
        SDL_Log("canvas: %dx%d px", event.window.data1, event.window.data2);
        break;
    case SDL_EVENT_WINDOW_HIDDEN:
        // The tab was hidden (visibilitychange). Events are dispatched before
        // a frame, and a hidden tab gets few or no frames, so this can arrive
        // late or together with SHOWN.
        hidden_ = true;
        hiddenAtNs_ = event.window.timestamp;
        SDL_Log("tab hidden");
        break;
    case SDL_EVENT_WINDOW_SHOWN:
        // SDL also sends SHOWN when the window is created; only a return
        // from a hidden tab matters here.
        if (hidden_) {
            hidden_ = false;
            // Restart the frame clock so the first frame back does not count
            // the hidden time as dt.
            const std::uint64_t now = SDL_GetTicksNS();
            SDL_Log("tab shown after %.1f s hidden; frame clock restarted",
                    static_cast<double>(event.window.timestamp - hiddenAtNs_) / 1e9);
            clock_.restart(now);
            resetLoopStats(now);
        }
        break;
    default:
        break;
    }
    InputEvent input;
    if (overlayOwns || !sdl::toInputEvent(event, input)) {
        return SDL_APP_CONTINUE;
    }
    audio::onEvent(input);
    if (scene_) {
        scene_->onEvent(input);
    }
    return SDL_APP_CONTINUE;
}

SDL_AppResult App::iterate() {
    const SDL_AppResult gameResult = toSdl(game_->update());
    if (gameResult != SDL_APP_CONTINUE) {
        return gameResult;
    }
    audio::update();
    // A hidden tab takes no simulation steps (docs/architecture.md, Frame).
    // Browsers usually stop frames there, but a throttled frame can still run.
    if (hidden_) {
        return SDL_APP_CONTINUE;
    }
    scene_->frameStart();
    const std::uint64_t now = SDL_GetTicksNS();
    const int steps = clock_.advance(now);
    for (int i = 0; i < steps; ++i) {
        scene_->fixedUpdate(FixedStepClock::kStepSeconds);
    }
    // No draw and no present when the canvas has no pixels.
    if (renderer_.beginFrame(game_->clearColor())) {
#ifdef VIBE_DEV
        overlay_.beginFrame();
#endif
        scene_->render(clock_.alpha());
#ifdef VIBE_DEV
        overlay_.endFrame();
#endif
        renderer_.endFrame();
    }
    scene_->frameEnd();
    recordLoopStats(now, steps);
    game_->flush();
    return SDL_APP_CONTINUE;
}

void App::quit(SDL_AppResult result) {
    if (game_) {
        game_->onQuit();
    }
    if (scene_) {
        scene_->onLeave();
        scene_.reset();
    }
    audio::shutdown();
#ifdef VIBE_DEV
    overlay_.shutdown();
#endif
    sdl::closeGamepads();
    renderer_.destroy();
    game_.reset();
    SDL_Log("%s", result == SDL_APP_FAILURE ? "stopped after an error" : "stopped");
}

void App::resetLoopStats(std::uint64_t nowNs) {
    statsStartNs_ = nowNs;
    statsFrames_ = 0;
    statsSteps_ = 0;
    statsMaxStepsPerFrame_ = 0;
}

void App::recordLoopStats(std::uint64_t nowNs, int steps) {
    ++statsFrames_;
    statsSteps_ += steps;
    statsMaxStepsPerFrame_ = std::max(statsMaxStepsPerFrame_, steps);
    const std::uint64_t elapsedNs = nowNs - statsStartNs_;
    if (elapsedNs >= kStatsIntervalNs) {
        // Plan 14: wasm memory only grows, so this is also the peak.
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                     "loop: %d steps in %d frames over %.2f s, max %d steps/frame, wasm %zu KB", statsSteps_,
                     statsFrames_, static_cast<double>(elapsedNs) / 1e9, statsMaxStepsPerFrame_,
                     platform::wasmMemoryBytes() / 1024);
        resetLoopStats(nowNs);
    }
}

} // namespace vibe

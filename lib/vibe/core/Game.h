#pragma once

#include <memory>

#include "vibe/core/HostConfig.h"
#include "vibe/core/Scene.h"
#include "vibe/audio/Audio.h"
#include "vibe/render/Renderer.h"

namespace vibe {

enum class AppResult { Continue, Success, Failure };

struct GameContext {
    Renderer& renderer;
    const HostConfig& config;
    int argc = 0;
    char** argv = nullptr;

    bool hasArgument(const char* name) const;
};

class Game {
public:
    virtual ~Game() = default;

    virtual Color clearColor() const { return Color{20, 26, 46, 255}; }
    virtual audio::CueConfig audioCues() const { return {}; }
    virtual std::unique_ptr<Scene> createInitialScene(const GameContext& context) = 0;
    virtual void onStart(const GameContext& /*context*/) {}
    virtual AppResult update() { return AppResult::Continue; }
    virtual void flush() {}
    virtual void onQuit() {}
};

std::unique_ptr<Game> createGame();

} // namespace vibe

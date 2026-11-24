#include <memory>

#include "PlayScene.h"
#include "vibe/core/Game.h"

namespace game {
namespace {

class SampleGame final : public vibe::Game {
public:
    std::unique_ptr<vibe::Scene> createInitialScene(const vibe::GameContext& context) override {
        return std::make_unique<PlayScene>(context.renderer, context.config);
    }
};

} // namespace
} // namespace game

std::unique_ptr<vibe::Game> vibe::createGame() {
    return std::make_unique<game::SampleGame>();
}

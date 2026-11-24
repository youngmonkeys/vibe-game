#include "vibe/node/NodeScene.h"

namespace vibe {

NodeScene::NodeScene(Renderer& renderer) : renderer_(renderer), root_(std::make_unique<Node>("root")) {}

NodeScene::~NodeScene() {
    root_->exitTree();
}

void NodeScene::onEnter() {
    root_->enterTree();
}

void NodeScene::onLeave() {
    root_->exitTree();
}

void NodeScene::onEvent(const SDL_Event& event) {
    root_->dispatchEvent(event);
}

void NodeScene::fixedUpdate(float stepSeconds) {
    root_->fixedUpdate(stepSeconds);
}

void NodeScene::render(float alpha) {
    root_->render(renderer_, alpha);
}

void NodeScene::frameEnd() {
    root_->flushPending();
}

} // namespace vibe

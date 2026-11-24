#pragma once

#include <memory>

#include "vibe/core/Input.h"
#include "vibe/core/Scene.h"
#include "vibe/node/Node.h"

namespace vibe {

class Renderer;

class NodeScene : public Scene {
public:
    explicit NodeScene(Renderer& renderer);
    ~NodeScene() override;

    Node& root() { return *root_; }
    const Node& root() const { return *root_; }
    Renderer& renderer() { return renderer_; }

    void onEnter() override;
    void onLeave() override;
    void onEvent(const InputEvent& event) override;
    void fixedUpdate(float stepSeconds) override;
    void render(float alpha) override;
    void frameEnd() override;

private:
    Renderer& renderer_;
    std::unique_ptr<Node> root_;
};

} // namespace vibe

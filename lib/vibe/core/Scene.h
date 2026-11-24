#pragma once

#include <SDL3/SDL_events.h>

namespace vibe {

// One screen of the game. App owns the current scene and drives it
// (docs/architecture.md, Scenes).
class Scene {
public:
    virtual ~Scene() = default;

    virtual void onEnter() {}
    virtual void onLeave() {}
    virtual void onEvent(const SDL_Event& /*event*/) {}
    // Once per frame before the fixed steps: drain the network (plan 07).
    virtual void frameStart() {}
    // One 40 Hz simulation step. No allocations here.
    virtual void fixedUpdate(float /*stepSeconds*/) {}
    // `alpha` is how far the frame is between the last step and the next, in
    // [0, 1). App calls it between Renderer::beginFrame and endFrame, and
    // skips it for a hidden tab or a canvas with no pixels.
    virtual void render(float /*alpha*/) {}
    // Once per frame after the present: send what the frame queued.
    virtual void frameEnd() {}
};

// Plan 01's scene: nothing but the clear color App presents every frame.
class EmptyScene final : public Scene {};

} // namespace vibe

#include "vibe/core/SceneStack.h"

namespace vibe {

SceneStack::~SceneStack() {
    pending_.clear();
    while (!scenes_.empty()) {
        popNow();
    }
}

void SceneStack::push(std::unique_ptr<Scene> scene) {
    if (scene) {
        pending_.push_back({Op::Push, std::move(scene)});
    }
}

void SceneStack::replace(std::unique_ptr<Scene> scene) {
    if (scene) {
        pending_.push_back({Op::Replace, std::move(scene)});
    }
}

void SceneStack::pop() {
    pending_.push_back({Op::Pop, nullptr});
}

void SceneStack::clear() {
    pending_.push_back({Op::Clear, nullptr});
}

void SceneStack::popNow() {
    std::unique_ptr<Scene> scene = std::move(scenes_.back());
    scenes_.pop_back();
    scene->onLeave();
}

void SceneStack::apply() {
    if (depth_ > 0) {
        return;
    }
    Running running(depth_);
    while (!pending_.empty()) {
        std::vector<Change> changes = std::move(pending_);
        pending_.clear();
        for (Change& change : changes) {
            switch (change.op) {
            case Op::Replace:
                if (!scenes_.empty()) {
                    popNow();
                }
                [[fallthrough]];
            case Op::Push:
                scenes_.push_back(std::move(change.scene));
                scenes_.back()->onEnter();
                break;
            case Op::Pop:
                if (!scenes_.empty()) {
                    popNow();
                }
                break;
            case Op::Clear:
                while (!scenes_.empty()) {
                    popNow();
                }
                break;
            }
        }
    }
}

void SceneStack::onEvent(const InputEvent& event) {
    if (Scene* scene = top()) {
        Running running(depth_);
        scene->onEvent(event);
    }
}

void SceneStack::frameStart() {
    Running running(depth_);
    for (const auto& scene : scenes_) {
        scene->frameStart();
    }
}

void SceneStack::fixedUpdate(float stepSeconds) {
    if (Scene* scene = top()) {
        Running running(depth_);
        scene->fixedUpdate(stepSeconds);
    }
}

void SceneStack::render(float alpha) {
    if (scenes_.empty()) {
        return;
    }
    std::size_t first = scenes_.size() - 1;
    while (first > 0 && !scenes_[first]->opaque()) {
        --first;
    }
    Running running(depth_);
    for (std::size_t i = first; i < scenes_.size(); ++i) {
        scenes_[i]->render(alpha);
    }
}

void SceneStack::frameEnd() {
    Running running(depth_);
    for (auto it = scenes_.rbegin(); it != scenes_.rend(); ++it) {
        (*it)->frameEnd();
    }
}

} // namespace vibe

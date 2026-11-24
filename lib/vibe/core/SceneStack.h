#pragma once

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

#include "vibe/core/Scene.h"

namespace vibe {

class SceneStack {
public:
    SceneStack() = default;
    SceneStack(const SceneStack&) = delete;
    SceneStack& operator=(const SceneStack&) = delete;
    ~SceneStack();

    void push(std::unique_ptr<Scene> scene);
    void replace(std::unique_ptr<Scene> scene);
    void pop();
    void clear();

    template <class T, class... Args>
    T& push(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *scene;
        push(std::move(scene));
        return ref;
    }

    template <class T, class... Args>
    T& replace(Args&&... args) {
        auto scene = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *scene;
        replace(std::move(scene));
        return ref;
    }

    void apply();

    bool empty() const { return scenes_.empty(); }
    std::size_t size() const { return scenes_.size(); }
    Scene* top() const { return scenes_.empty() ? nullptr : scenes_.back().get(); }
    bool hasPending() const { return !pending_.empty(); }

    void onEvent(const InputEvent& event);
    void frameStart();
    void fixedUpdate(float stepSeconds);
    void render(float alpha);
    void frameEnd();

private:
    enum class Op { Push, Replace, Pop, Clear };

    struct Change {
        Op op;
        std::unique_ptr<Scene> scene;
    };

    class Running {
    public:
        explicit Running(int& depth) : depth_(depth) { ++depth_; }
        ~Running() { --depth_; }

    private:
        int& depth_;
    };

    void popNow();

    std::vector<std::unique_ptr<Scene>> scenes_;
    std::vector<Change> pending_;
    int depth_ = 0;
};

} // namespace vibe

#pragma once

#include <SDL3/SDL_events.h>

#include <cstddef>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "vibe/math/Transform.h"

namespace vibe {

class Renderer;

class Node {
public:
    Node() = default;
    explicit Node(std::string name) : name_(std::move(name)) {}
    virtual ~Node();

    Node(const Node&) = delete;
    Node& operator=(const Node&) = delete;

    const std::string& name() const { return name_; }
    void setName(std::string name) { name_ = std::move(name); }

    Node* parent() const { return parent_; }
    bool inTree() const { return inTree_; }
    std::size_t childCount() const;
    Node* childAt(std::size_t index) const;
    Node* findChild(std::string_view name) const;
    Node* findDescendant(std::string_view name) const;

    template <typename T, typename... Args>
    T& emplaceChild(Args&&... args) {
        auto child = std::make_unique<T>(std::forward<Args>(args)...);
        T& ref = *child;
        addChild(std::move(child));
        return ref;
    }
    Node& addChild(std::unique_ptr<Node> child);
    std::unique_ptr<Node> detachChild(Node& child);
    std::unique_ptr<Node> detach();
    void queueFree();
    bool isQueuedForFree() const { return queuedForFree_; }

    const Transform& transform() const { return transform_; }
    void setTransform(const Transform& transform);
    Vec3 position() const { return transform_.position; }
    void setPosition(Vec3 position);
    void setPosition(float x, float y, float z = 0.0f) { setPosition(Vec3{x, y, z}); }
    Quat rotation() const { return transform_.rotation; }
    void setRotation(Quat rotation);
    void setRotationZ(float radians) { setRotation(Quat::aroundZ(radians)); }
    Vec3 scale() const { return transform_.scale; }
    void setScale(Vec3 scale);
    void setScale(float uniform) { setScale(Vec3{uniform, uniform, uniform}); }
    void snapInterpolation();

    Mat4 worldMatrix() const;
    Vec3 toWorld(Vec3 local) const { return worldMatrix().transformPoint(local); }
    Vec3 toLocal(Vec3 world) const { return worldMatrix().affineInverse().transformPoint(world); }

    bool visible() const { return visible_; }
    void setVisible(bool visible) { visible_ = visible; }
    bool active() const { return active_; }
    void setActive(bool active) { active_ = active; }

    void enterTree();
    void exitTree();
    bool dispatchEvent(const SDL_Event& event);
    void fixedUpdate(float stepSeconds);
    void render(Renderer& renderer, float alpha);
    void flushPending();

protected:
    virtual void onEnterTree() {}
    virtual void onExitTree() {}
    virtual bool onEvent(const SDL_Event& /*event*/) { return false; }
    virtual void onFixedUpdate(float /*stepSeconds*/) {}
    virtual void onRender(Renderer& /*renderer*/, const Mat4& /*world*/) {}

private:
    class IterationGuard;

    void transformChanged();
    void markPending();
    void snapshotSubtree();
    void updateSubtree(float stepSeconds);
    void renderSubtree(Renderer& renderer, float alpha, const Mat4& parentWorld);
    std::size_t indexOf(const Node& child) const;
    std::unique_ptr<Node> takeChildAt(std::size_t index);

    std::string name_;
    Node* parent_ = nullptr;
    std::vector<std::unique_ptr<Node>> children_;
    Transform transform_;
    Transform previous_;
    int iterating_ = 0;
    bool inTree_ = false;
    bool visible_ = true;
    bool active_ = true;
    bool queuedForFree_ = false;
    bool pending_ = false;
};

} // namespace vibe

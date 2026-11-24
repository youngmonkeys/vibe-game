#include "vibe/node/Node.h"

#include <algorithm>

namespace vibe {

class Node::IterationGuard {
public:
    explicit IterationGuard(Node& node) : node_(node) { ++node_.iterating_; }
    ~IterationGuard() { --node_.iterating_; }

    IterationGuard(const IterationGuard&) = delete;
    IterationGuard& operator=(const IterationGuard&) = delete;

private:
    Node& node_;
};

Node::~Node() {
    for (auto& child : children_) {
        if (child) {
            child->parent_ = nullptr;
        }
    }
}

std::size_t Node::childCount() const {
    return static_cast<std::size_t>(std::count_if(children_.begin(), children_.end(),
                                                  [](const auto& child) { return child != nullptr; }));
}

Node* Node::childAt(std::size_t index) const {
    for (const auto& child : children_) {
        if (child) {
            if (index == 0) {
                return child.get();
            }
            --index;
        }
    }
    return nullptr;
}

Node* Node::findChild(std::string_view name) const {
    for (const auto& child : children_) {
        if (child && !child->queuedForFree_ && child->name_ == name) {
            return child.get();
        }
    }
    return nullptr;
}

Node* Node::findDescendant(std::string_view name) const {
    for (const auto& child : children_) {
        if (!child || child->queuedForFree_) {
            continue;
        }
        if (child->name_ == name) {
            return child.get();
        }
        if (Node* found = child->findDescendant(name)) {
            return found;
        }
    }
    return nullptr;
}

Node& Node::addChild(std::unique_ptr<Node> child) {
    Node& ref = *child;
    child->parent_ = this;
    child->snapInterpolation();
    children_.push_back(std::move(child));
    if (inTree_) {
        ref.enterTree();
    }
    return ref;
}

std::unique_ptr<Node> Node::detachChild(Node& child) {
    const std::size_t index = indexOf(child);
    if (index == children_.size()) {
        return nullptr;
    }
    return takeChildAt(index);
}

std::unique_ptr<Node> Node::detach() {
    return parent_ ? parent_->detachChild(*this) : nullptr;
}

void Node::queueFree() {
    if (queuedForFree_ || !parent_) {
        return;
    }
    queuedForFree_ = true;
    parent_->markPending();
}

void Node::setTransform(const Transform& transform) {
    transform_ = transform;
    transformChanged();
}

void Node::setPosition(Vec3 position) {
    transform_.position = position;
    transformChanged();
}

void Node::setRotation(Quat rotation) {
    transform_.rotation = rotation;
    transformChanged();
}

void Node::setScale(Vec3 scale) {
    transform_.scale = scale;
    transformChanged();
}

void Node::snapInterpolation() {
    previous_ = transform_;
}

Mat4 Node::worldMatrix() const {
    const Mat4 local = transform_.matrix();
    return parent_ ? parent_->worldMatrix() * local : local;
}

void Node::enterTree() {
    if (inTree_) {
        return;
    }
    inTree_ = true;
    onEnterTree();
    IterationGuard guard(*this);
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (Node* child = children_[i].get()) {
            child->enterTree();
        }
    }
}

void Node::exitTree() {
    if (!inTree_) {
        return;
    }
    {
        IterationGuard guard(*this);
        for (std::size_t i = children_.size(); i > 0; --i) {
            if (Node* child = children_[i - 1].get()) {
                child->exitTree();
            }
        }
    }
    inTree_ = false;
    onExitTree();
}

bool Node::dispatchEvent(const SDL_Event& event) {
    if (!active_ || !visible_ || queuedForFree_) {
        return false;
    }
    {
        IterationGuard guard(*this);
        for (std::size_t i = children_.size(); i > 0; --i) {
            Node* child = children_[i - 1].get();
            if (child && child->dispatchEvent(event)) {
                return true;
            }
        }
    }
    return onEvent(event);
}

void Node::fixedUpdate(float stepSeconds) {
    snapshotSubtree();
    updateSubtree(stepSeconds);
}

void Node::render(Renderer& renderer, float alpha) {
    renderSubtree(renderer, alpha, parent_ ? parent_->worldMatrix() : Mat4{});
}

void Node::flushPending() {
    if (!pending_ || iterating_ > 0) {
        return;
    }
    pending_ = false;
    for (std::size_t i = 0; i < children_.size();) {
        Node* child = children_[i].get();
        if (child && child->queuedForFree_) {
            takeChildAt(i);
            continue;
        }
        if (!child) {
            children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(i));
            continue;
        }
        child->flushPending();
        if (child->pending_) {
            pending_ = true;
        }
        ++i;
    }
}

void Node::transformChanged() {
    if (!inTree_) {
        previous_ = transform_;
    }
}

void Node::markPending() {
    for (Node* node = this; node && !node->pending_; node = node->parent_) {
        node->pending_ = true;
    }
}

void Node::snapshotSubtree() {
    if (queuedForFree_) {
        return;
    }
    previous_ = transform_;
    for (auto& child : children_) {
        if (child) {
            child->snapshotSubtree();
        }
    }
}

void Node::updateSubtree(float stepSeconds) {
    if (!active_ || queuedForFree_) {
        return;
    }
    onFixedUpdate(stepSeconds);
    IterationGuard guard(*this);
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (Node* child = children_[i].get()) {
            child->updateSubtree(stepSeconds);
        }
    }
}

void Node::renderSubtree(Renderer& renderer, float alpha, const Mat4& parentWorld) {
    if (!visible_ || queuedForFree_) {
        return;
    }
    const Mat4 world = parentWorld * Transform::lerp(previous_, transform_, alpha).matrix();
    onRender(renderer, world);
    IterationGuard guard(*this);
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (Node* child = children_[i].get()) {
            child->renderSubtree(renderer, alpha, world);
        }
    }
}

std::size_t Node::indexOf(const Node& child) const {
    for (std::size_t i = 0; i < children_.size(); ++i) {
        if (children_[i].get() == &child) {
            return i;
        }
    }
    return children_.size();
}

std::unique_ptr<Node> Node::takeChildAt(std::size_t index) {
    std::unique_ptr<Node> child = std::move(children_[index]);
    if (iterating_ > 0) {
        markPending();
    } else {
        children_.erase(children_.begin() + static_cast<std::ptrdiff_t>(index));
    }
    child->parent_ = nullptr;
    child->queuedForFree_ = false;
    if (child->inTree_) {
        child->exitTree();
    }
    return child;
}

} // namespace vibe

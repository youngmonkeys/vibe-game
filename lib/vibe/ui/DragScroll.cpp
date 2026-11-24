#include "vibe/ui/DragScroll.h"

#include <SDL3/SDL_timer.h>

#include <cmath>

namespace vibe::ui {
namespace {

// A fling starts above this speed and stops below the second; it loses about
// 97% of its speed per second.
constexpr float kFlingStartPxPerSecond = 300.0f;
constexpr float kFlingStopPxPerSecond = 40.0f;
constexpr float kFlingDecayPerSecond = 3.5f;
// A pause longer than this before the release means no fling.
constexpr std::uint64_t kFlingStaleNs = 100'000'000;
// Where a drag's release goes so no button takes it as a click.
constexpr float kOffScreen = -1.0e6f;

} // namespace

bool DragScroll::onEvent(InputEvent& event, const ScreenRect& area, float slopPx) {
    switch (event.type) {
    case EventType::MouseButtonDown:
        if (event.button != MouseButton::Left) {
            return false;
        }
        // A touch stops a fling, as on a phone.
        flinging_ = false;
        velocity_ = 0.0f;
        armed_ = area.contains(event.x, event.y);
        dragging_ = false;
        startPos_ = lastPos_ = along(event.x, event.y);
        lastNs_ = SDL_GetTicksNS();
        return false;
    case EventType::MouseMotion: {
        if (!armed_ || !event.leftHeld) {
            armed_ = false;
            return false;
        }
        const float pos = along(event.x, event.y);
        const std::uint64_t now = SDL_GetTicksNS();
        if (!dragging_) {
            if (std::fabs(pos - startPos_) <= slopPx) {
                return false;
            }
            dragging_ = true;
        }
        // The finger moving up (or left) moves the content forward.
        const float moved = lastPos_ - pos;
        pendingPx_ += moved;
        const float dt = static_cast<float>(now - lastNs_) / 1e9f;
        if (dt > 0.0f) {
            velocity_ = 0.6f * velocity_ + 0.4f * (moved / dt);
        }
        lastPos_ = pos;
        lastNs_ = now;
        return true;
    }
    case EventType::MouseButtonUp: {
        if (event.button != MouseButton::Left) {
            return false;
        }
        const bool wasDragging = dragging_;
        armed_ = false;
        dragging_ = false;
        if (!wasDragging) {
            return false;
        }
        const std::uint64_t now = SDL_GetTicksNS();
        if (now - lastNs_ < kFlingStaleNs && std::fabs(velocity_) > kFlingStartPxPerSecond) {
            flinging_ = true;
            flingNs_ = now;
        } else {
            velocity_ = 0.0f;
        }
        event.x = kOffScreen;
        event.y = kOffScreen;
        return true;
    }
    default:
        return false;
    }
}

int DragScroll::take(float rowPx) {
    if (flinging_) {
        const std::uint64_t now = SDL_GetTicksNS();
        const float dt = std::fmin(0.1f, static_cast<float>(now - flingNs_) / 1e9f);
        flingNs_ = now;
        pendingPx_ += velocity_ * dt;
        velocity_ *= std::exp(-kFlingDecayPerSecond * dt);
        if (std::fabs(velocity_) < kFlingStopPxPerSecond) {
            flinging_ = false;
            velocity_ = 0.0f;
        }
    }
    if (!(rowPx > 0.0f)) {
        return 0;
    }
    const int rows = static_cast<int>(pendingPx_ / rowPx);
    pendingPx_ -= static_cast<float>(rows) * rowPx;
    return rows;
}

void DragScroll::stop() {
    armed_ = false;
    dragging_ = false;
    flinging_ = false;
    pendingPx_ = 0.0f;
    velocity_ = 0.0f;
}

} // namespace vibe::ui

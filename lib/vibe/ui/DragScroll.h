#pragma once

#include <cstdint>

#include "vibe/core/Input.h"
#include "vibe/render/Renderer.h"

namespace vibe::ui {

// Drag to scroll a list, with the mouse or a finger (SDL turns the first
// finger into left-button mouse events), and a fling that slows down after
// the release. The lists scroll by whole rows; this turns pixels of drag
// into rows (take()).
//
// A press inside the area only arms a drag, so a tap still clicks the button
// under it. Once the pointer has moved more than the slop along the axis, it
// is a drag: the release is moved far off-screen in the event, so no button
// under it clicks (Button needs a release inside its rectangle).
class DragScroll {
public:
    enum class Axis { Vertical, Horizontal };

    explicit DragScroll(Axis axis = Axis::Vertical) : axis_(axis) {}

    // `event` is in canvas pixels and may be changed (see above). Returns true
    // for motion and the release of a drag: the caller may skip hover then.
    bool onEvent(InputEvent& event, const ScreenRect& area, float slopPx);
    // Rows to move since the last call, from dragging and from the fling.
    // Positive goes forward: the content moves up (or left), showing later
    // rows. `rowPx` is one row's pitch along the axis. Call once per frame.
    int take(float rowPx);
    // Drops the drag, the fling and the leftover pixels (a list reset).
    void stop();
    bool dragging() const { return dragging_; }

private:
    float along(float x, float y) const { return axis_ == Axis::Vertical ? y : x; }

    Axis axis_;
    bool armed_ = false;
    bool dragging_ = false;
    bool flinging_ = false;
    float startPos_ = 0.0f;
    float lastPos_ = 0.0f;
    std::uint64_t lastNs_ = 0;
    std::uint64_t flingNs_ = 0;
    // Content movement not yet turned into rows; positive goes forward.
    float pendingPx_ = 0.0f;
    // Pixels per second, positive forward: smoothed while dragging, then
    // the fling's speed.
    float velocity_ = 0.0f;
};

} // namespace vibe::ui

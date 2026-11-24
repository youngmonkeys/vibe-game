#pragma once

#include <cstdint>

namespace vibe {

// Turns frame times into a whole number of 0.025 s simulation steps
// (docs/architecture.md, Frame). Integer nanoseconds, so the accumulator never
// drifts. A frame longer than 0.1 s counts as 0.1 s, which caps a frame at 4
// steps after a stall.
class FixedStepClock {
public:
    static constexpr std::uint64_t kStepNs = 25'000'000;
    static constexpr std::uint64_t kMaxFrameNs = 100'000'000;
    static constexpr float kStepSeconds = 0.025f;

    // Forget the time since the last frame, for example after a hidden tab.
    void restart(std::uint64_t nowNs) { lastNs_ = nowNs; }

    // Returns how many fixed steps this frame runs.
    int advance(std::uint64_t nowNs) {
        std::uint64_t frameNs = nowNs - lastNs_;
        lastNs_ = nowNs;
        if (frameNs > kMaxFrameNs) {
            frameNs = kMaxFrameNs;
        }
        accumulatorNs_ += frameNs;
        int steps = 0;
        while (accumulatorNs_ >= kStepNs) {
            accumulatorNs_ -= kStepNs;
            ++steps;
        }
        return steps;
    }

    // Interpolation factor for render, in [0, 1).
    float alpha() const {
        return static_cast<float>(accumulatorNs_) / static_cast<float>(kStepNs);
    }

private:
    std::uint64_t lastNs_ = 0;
    std::uint64_t accumulatorNs_ = 0;
};

} // namespace vibe

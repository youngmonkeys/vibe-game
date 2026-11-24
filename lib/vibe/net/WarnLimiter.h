#pragma once

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <cstdint>

namespace vibe::net {

// One warning, then at most one every few seconds with a count of what was
// dropped in between, so a flood of bad frames cannot flood the console.
class WarnLimiter {
public:
    static constexpr std::uint64_t kIntervalMs = 5000;

    // `what` and `reason` are static text that never quotes a payload.
    void warn(const char* what, const char* reason) {
        const std::uint64_t now = SDL_GetTicks();
        if (lastMs_ != 0 && now - lastMs_ < kIntervalMs) {
            ++suppressed_;
            return;
        }
        if (suppressed_ > 0) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: dropped %s: %s (%d more dropped since the last warning)",
                        what, reason, suppressed_);
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: dropped %s: %s", what, reason);
        }
        lastMs_ = now == 0 ? 1 : now;
        suppressed_ = 0;
    }

private:
    std::uint64_t lastMs_ = 0;
    int suppressed_ = 0;
};

} // namespace vibe::net

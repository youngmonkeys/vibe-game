#include "vibe/ui/dev/DevHooks.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace vibe::ui::dev {
namespace {

struct Mark {
    std::string id;
    ScreenRect rect;
    unsigned frame = 0;
};

std::vector<Mark> gMarks;
unsigned gFrame = 1;

} // namespace

void markRect(const char* id, const ScreenRect& rect) {
    if (!id) {
        return;
    }
    for (Mark& mark : gMarks) {
        if (mark.id == id) {
            mark.rect = rect;
            mark.frame = gFrame;
            return;
        }
    }
    gMarks.push_back({id, rect, gFrame});
}

void beginFrame() {
    ++gFrame;
}

const char* rectOf(const char* id) {
    static char answer[96];
    answer[0] = '\0';
    for (const Mark& mark : gMarks) {
        // Drawn in the frame that just ended (or the one being drawn).
        if (id && mark.id == id && mark.frame + 1 >= gFrame) {
            std::snprintf(answer, sizeof answer, "%.1f %.1f %.1f %.1f", static_cast<double>(mark.rect.x),
                          static_cast<double>(mark.rect.y), static_cast<double>(mark.rect.w),
                          static_cast<double>(mark.rect.h));
            break;
        }
    }
    return answer;
}

} // namespace vibe::ui::dev

extern "C" const char* vibe_dev_ui_rect(const char* id) {
    return vibe::ui::dev::rectOf(id);
}

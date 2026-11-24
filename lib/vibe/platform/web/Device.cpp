#include "vibe/platform/Device.h"

#include <emscripten/emscripten.h>
#include <malloc.h>

namespace vibe::platform {
namespace {

EM_JS(int, vibe_device_mobile, (), {
    var ua = navigator.userAgent || "";
    if (/Android|iPhone|iPad|iPod|Mobile/i.test(ua)) {
        return 1;
    }
    // iPadOS asks for desktop pages by default and reports a Mac.
    return (/Macintosh/.test(ua) && navigator.maxTouchPoints > 1) ? 1 : 0;
});

// The insets live in Module.vibeSafe, refreshed by resize and
// orientationchange listeners, so a frame never forces a style recalculation.
EM_JS(double, vibe_device_safe, (int side), {
    if (!Module.vibeSafe) {
        var read = function () {
            var probe = document.getElementById('safe-probe');
            if (!probe) {
                Module.vibeSafe = [0, 0, 0, 0];
                return;
            }
            var s = getComputedStyle(probe);
            Module.vibeSafe = [parseFloat(s.paddingTop) || 0, parseFloat(s.paddingRight) || 0,
                               parseFloat(s.paddingBottom) || 0, parseFloat(s.paddingLeft) || 0];
        };
        read();
        window.addEventListener('resize', read);
        window.addEventListener('orientationchange', read);
    }
    return Module.vibeSafe[side];
});

EM_JS(double, vibe_device_memory, (), {
    return HEAP8.length;
});

} // namespace

bool isMobileUserAgent() {
    static const bool mobile = vibe_device_mobile() != 0;
    return mobile;
}

SafeInsets safeAreaInsets() {
    return SafeInsets{static_cast<float>(vibe_device_safe(0)), static_cast<float>(vibe_device_safe(1)),
                      static_cast<float>(vibe_device_safe(2)), static_cast<float>(vibe_device_safe(3))};
}

std::size_t wasmMemoryBytes() {
    return static_cast<std::size_t>(vibe_device_memory());
}

std::size_t heapInUseBytes() {
    return static_cast<std::size_t>(mallinfo().uordblks);
}

} // namespace vibe::platform

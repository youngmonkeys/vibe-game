#include "vibe/platform/TouchText.h"

#include <emscripten/emscripten.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>

namespace vibe::platform {
namespace {

EM_JS(void, vibe_tt_install, (), {
    if (Module.vibeTT) {
        return;
    }
    var proxy = document.getElementById('text-proxy');
    var canvas = Module.canvas;
    var tt = {fields: [], focus: -1, value: null, enter: false, blur: false, proxy: proxy};
    Module.vibeTT = tt;
    if (!proxy || !canvas) {
        return;
    }
    // Canvas pixels to CSS pixels.
    var cssRect = function (f) {
        var k = canvas.clientWidth / Math.max(1, canvas.width);
        var r = canvas.getBoundingClientRect();
        return {x: r.left + f.x * k, y: r.top + f.y * k, w: f.w * k, h: f.h * k};
    };
    canvas.addEventListener('touchend', function (event) {
        var touch = event.changedTouches && event.changedTouches[0];
        if (!touch) {
            return;
        }
        for (var i = 0; i < tt.fields.length; ++i) {
            var r = cssRect(tt.fields[i]);
            if (touch.clientX >= r.x && touch.clientX < r.x + r.w && touch.clientY >= r.y && touch.clientY < r.y + r.h) {
                // Over the field, so iOS keeps that spot above the keyboard.
                proxy.style.left = r.x + 'px';
                proxy.style.top = r.y + 'px';
                proxy.style.width = r.w + 'px';
                proxy.style.height = r.h + 'px';
                var type = tt.fields[i].password ? 'password' : 'text';
                if (proxy.type !== type) {
                    // Firefox blurs a focused input whose type changes, after
                    // this handler; change it unfocused and focus again below.
                    if (document.activeElement === proxy) {
                        proxy.blur();
                    }
                    proxy.type = type;
                }
                proxy.value = "";
                tt.value = null;
                tt.focus = i;
                tt.blur = false;
                // Inside the touch handler: this is what opens the keyboard.
                proxy.focus({preventScroll: true});
                // No compatibility mousedown afterwards: it would move focus
                // to the canvas (tabindex -1) and close the keyboard again.
                event.preventDefault();
                return;
            }
        }
        if (document.activeElement === proxy) {
            proxy.blur();
        }
    });
    proxy.addEventListener('input', function () {
        tt.value = proxy.value;
    });
    var stop = function (event) {
        // SDL's key listeners are on window; the proxy's keys end here.
        event.stopPropagation();
        if (event.type === 'keydown' && (event.key === 'Enter' || event.keyCode === 13)) {
            tt.enter = true;
            event.preventDefault();
        }
    };
    proxy.addEventListener('keydown', stop);
    proxy.addEventListener('keyup', stop);
    proxy.addEventListener('keypress', stop);
    proxy.addEventListener('blur', function () {
        tt.blur = true;
    });
});

EM_JS(void, vibe_tt_fields, (const float* data, int count), {
    var tt = Module.vibeTT;
    if (!tt) {
        return;
    }
    var list = [];
    for (var i = 0; i < count; ++i) {
        var p = (data >> 2) + i * 5;
        list.push({x: HEAPF32[p], y: HEAPF32[p + 1], w: HEAPF32[p + 2], h: HEAPF32[p + 3], password: HEAPF32[p + 4] > 0});
    }
    tt.fields = list;
});

EM_JS(int, vibe_tt_take_focus, (), {
    var tt = Module.vibeTT;
    if (!tt || tt.focus < 0) {
        return -1;
    }
    var i = tt.focus;
    tt.focus = -1;
    return i;
});

EM_JS(void, vibe_tt_set_value, (const char* text, int maxChars), {
    var tt = Module.vibeTT;
    if (!tt || !tt.proxy) {
        return;
    }
    tt.proxy.maxLength = maxChars;
    tt.proxy.value = UTF8ToString(text);
    tt.value = null;
    var end = tt.proxy.value.length;
    try {
        tt.proxy.setSelectionRange(end, end);
    } catch (e) {
        // type=password has no selection API in some browsers.
    }
});

EM_JS(char*, vibe_tt_take_value, (), {
    var tt = Module.vibeTT;
    if (!tt || tt.value === null) {
        return 0;
    }
    var v = tt.value;
    tt.value = null;
    return stringToNewUTF8(v);
});

EM_JS(int, vibe_tt_take_flag, (int which), {
    var tt = Module.vibeTT;
    if (!tt) {
        return 0;
    }
    var key = which === 0 ? 'enter' : 'blur';
    var on = tt[key];
    tt[key] = false;
    return on ? 1 : 0;
});

EM_JS(void, vibe_tt_blur, (), {
    var tt = Module.vibeTT;
    if (tt && tt.proxy && document.activeElement === tt.proxy) {
        tt.proxy.value = "";
        tt.proxy.blur();
    }
});

EM_JS(int, vibe_tt_active, (), {
    var tt = Module.vibeTT;
    return tt && tt.proxy && document.activeElement === tt.proxy ? 1 : 0;
});

// What was published last, so an unchanged list is not sent again.
std::array<float, kMaxFields * 5> published{};
int publishedCount = -1;

} // namespace

void installTouchText() {
    vibe_tt_install();
}

void publishFields(const FieldRect* fields, int count) {
    count = std::clamp(count, 0, kMaxFields);
    std::array<float, kMaxFields * 5> data{};
    for (int i = 0; i < count; ++i) {
        const FieldRect& f = fields[i];
        float* d = &data[static_cast<std::size_t>(i * 5)];
        d[0] = f.x;
        d[1] = f.y;
        d[2] = f.w;
        d[3] = f.h;
        d[4] = f.password ? 1.0f : 0.0f;
    }
    if (count == publishedCount && data == published) {
        return;
    }
    published = data;
    publishedCount = count;
    vibe_tt_fields(published.data(), count);
}

int takeFocus() {
    return vibe_tt_take_focus();
}

void setValue(std::string_view text, int maxChars) {
    // A password passes through here; leave no copy behind.
    std::string copy(text);
    vibe_tt_set_value(copy.c_str(), std::max(1, maxChars));
    std::fill(copy.begin(), copy.end(), '\0');
}

bool takeValue(std::string& out) {
    char* text = vibe_tt_take_value();
    if (!text) {
        return false;
    }
    out.assign(text);
    std::fill(text, text + out.size(), '\0');
    std::free(text);
    return true;
}

bool takeEnter() {
    return vibe_tt_take_flag(0) != 0;
}

bool takeBlur() {
    return vibe_tt_take_flag(1) != 0;
}

void blur() {
    vibe_tt_blur();
}

bool active() {
    return vibe_tt_active() != 0;
}

} // namespace vibe::platform

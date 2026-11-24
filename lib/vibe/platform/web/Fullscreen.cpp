#include "vibe/platform/Fullscreen.h"

#include <emscripten/emscripten.h>

#include <array>

namespace vibe::platform {
namespace {

EM_JS(void, vibe_fs_install, (), {
    if (Module.vibeFS) {
        return;
    }
    var fs = {button: null};
    Module.vibeFS = fs;
    var canvas = Module.canvas;
    if (!canvas) {
        return;
    }
    var root = document.documentElement;
    var hit = function (clientX, clientY) {
        var b = fs.button;
        if (!b) {
            return false;
        }
        // Canvas pixels to CSS pixels.
        var k = canvas.clientWidth / Math.max(1, canvas.width);
        var r = canvas.getBoundingClientRect();
        var x = r.left + b.x * k;
        var y = r.top + b.y * k;
        return clientX >= x && clientX < x + b.w * k && clientY >= y && clientY < y + b.h * k;
    };
    var warn = function (what, error) {
        console.warn('WARN: page: ' + what + ': ' + (error && error.message ? error.message : error));
    };
    var toggle = function () {
        var request = root.requestFullscreen || root.webkitRequestFullscreen;
        if (document.fullscreenElement || document.webkitFullscreenElement) {
            var exit = document.exitFullscreen || document.webkitExitFullscreen;
            try {
                var left = exit.call(document);
                if (left && left.catch) {
                    left.catch(function (e) { warn('leaving fullscreen failed', e); });
                }
            } catch (e) {
                warn('leaving fullscreen failed', e);
            }
            return;
        }
        if (!request) {
            return;
        }
        try {
            var entered = request.call(root, {navigationUI: 'hide'});
            if (entered && entered.then) {
                entered.then(function () {
                    if (screen.orientation && screen.orientation.lock) {
                        screen.orientation.lock('landscape').catch(function () {});
                    }
                }, function (e) { warn('fullscreen refused', e); });
            }
        } catch (e) {
            warn('fullscreen refused', e);
        }
    };
    // Both are user activations: a touch ends with `touchend`, a mouse
    // starts with `pointerdown` (a touch's own pointerdown is not one).
    canvas.addEventListener('touchend', function (event) {
        var touch = event.changedTouches && event.changedTouches[0];
        if (touch && hit(touch.clientX, touch.clientY)) {
            toggle();
        }
    });
    canvas.addEventListener('pointerdown', function (event) {
        if (event.pointerType === 'mouse' && event.button === 0 && hit(event.clientX, event.clientY)) {
            toggle();
        }
    });
});

EM_JS(int, vibe_fs_supported, (), {
    var root = document.documentElement;
    var enabled = document.fullscreenEnabled || document.webkitFullscreenEnabled;
    return enabled && (root.requestFullscreen || root.webkitRequestFullscreen) ? 1 : 0;
});

EM_JS(int, vibe_fs_active, (), {
    return document.fullscreenElement || document.webkitFullscreenElement ? 1 : 0;
});

EM_JS(int, vibe_fs_app, (), {
    var mode = function (m) {
        return window.matchMedia && window.matchMedia('(display-mode: ' + m + ')').matches;
    };
    // `display-mode: fullscreen` also matches while the button's own
    // fullscreen is on; only without a fullscreen element is it the app's.
    var appFullscreen = mode('fullscreen') && !(document.fullscreenElement || document.webkitFullscreenElement);
    return mode('standalone') || appFullscreen || window.navigator.standalone === true ? 1 : 0;
});

EM_JS(int, vibe_fs_iphone, (), {
    return /iPhone|iPod/.test(navigator.userAgent) ? 1 : 0;
});

EM_JS(void, vibe_fs_button, (float x, float y, float w, float h), {
    if (Module.vibeFS) {
        Module.vibeFS.button = w > 0 ? {x: x, y: y, w: w, h: h} : null;
    }
});

std::array<float, 4> published{-1.0f, -1.0f, -1.0f, -1.0f};

} // namespace

void installFullscreen() {
    vibe_fs_install();
}

bool fullscreenSupported() {
    return vibe_fs_supported() != 0;
}

bool isFullscreen() {
    return vibe_fs_active() != 0;
}

bool runningAsApp() {
    return vibe_fs_app() != 0;
}

bool isIphone() {
    return vibe_fs_iphone() != 0;
}

void publishButton(float x, float y, float w, float h) {
    if (w <= 0.0f) {
        x = y = w = h = 0.0f;
    }
    const std::array<float, 4> rect{x, y, w, h};
    if (rect == published) {
        return;
    }
    published = rect;
    vibe_fs_button(x, y, w, h);
}

} // namespace vibe::platform

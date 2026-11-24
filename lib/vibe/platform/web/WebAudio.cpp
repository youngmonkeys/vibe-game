#include "vibe/platform/WebAudio.h"

#include <emscripten/emscripten.h>

namespace vibe::platform {
namespace {

EM_JS(int, vibe_web_audio_running, (), {
    var sdl = Module.SDL3;
    if (sdl && sdl.audioContext) {
        return sdl.audioContext.state === 'running' ? 1 : 0;
    }
    return (typeof navigator.userActivation !== 'undefined' && navigator.userActivation.hasBeenActive) ? 1 : 0;
});

EM_JS(void, vibe_web_audio_gesture_resume, (), {
    if (Module.vibeGestureResume) {
        return;
    }
    Module.vibeGestureResume = true;
    var resume = function () {
        var sdl = Module.SDL3;
        var context = sdl && sdl.audioContext;
        if (context && context.state !== 'running' && context.state !== 'closed') {
            context.resume().catch(function () {});
        }
    };
    ['touchend', 'pointerup', 'keydown', 'click'].forEach(function (type) {
        window.addEventListener(type, resume, true);
    });
});

} // namespace

void installGestureResume() {
    vibe_web_audio_gesture_resume();
}

bool webAudioRunning() {
    return vibe_web_audio_running() != 0;
}

} // namespace vibe::platform

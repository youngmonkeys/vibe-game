#include "vibe/platform/Paste.h"

#include <emscripten/emscripten.h>

#include <cstdlib>

namespace vibe::platform {
namespace {

EM_JS(void, vibe_paste_install, (), {
    if (Module.vibePasteInstalled) {
        return;
    }
    Module.vibePasteInstalled = true;
    Module.vibePasted = "";
    window.addEventListener('paste', function (event) {
        var text = event.clipboardData ? event.clipboardData.getData("text/plain") : "";
        if (text) {
            // Bounded: a field takes at most a few hundred characters.
            Module.vibePasted = (Module.vibePasted + text).slice(0, 4096);
            event.preventDefault();
        }
    });
});

EM_JS(char*, vibe_paste_take, (), {
    var text = Module.vibePasted || "";
    Module.vibePasted = "";
    return text ? stringToNewUTF8(text) : 0;
});

} // namespace

void installPasteListener() {
    vibe_paste_install();
}

bool takePastedText(std::string& out) {
    char* text = vibe_paste_take();
    if (!text) {
        return false;
    }
    out.assign(text);
    std::free(text);
    return true;
}

} // namespace vibe::platform

#include "vibe/platform/Paste.h"

#include <SDL3/SDL_clipboard.h>
#include <SDL3/SDL_events.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstring>

namespace vibe::platform {
namespace {

constexpr std::size_t kMaxPasteBytes = 4096;

bool gInstalled = false;
std::string gPasted;

bool SDLCALL onEvent(void*, SDL_Event* event) {
    if (event->type != SDL_EVENT_KEY_DOWN || event->key.key != SDLK_V) {
        return true;
    }
    const SDL_Keymod mods = event->key.mod;
#if defined(__APPLE__)
    const bool paste = (mods & SDL_KMOD_GUI) != 0;
#else
    const bool paste = (mods & SDL_KMOD_CTRL) != 0;
#endif
    if (!paste || !SDL_HasClipboardText()) {
        return true;
    }
    char* text = SDL_GetClipboardText();
    if (text) {
        const std::size_t length = std::strlen(text);
        const std::size_t room = kMaxPasteBytes - std::min(kMaxPasteBytes, gPasted.size());
        gPasted.append(text, std::min(length, room));
        std::fill(text, text + length, '\0');
        SDL_free(text);
    }
    return true;
}

} // namespace

void installPasteListener() {
    if (gInstalled) {
        return;
    }
    gInstalled = SDL_AddEventWatch(onEvent, nullptr);
}

bool takePastedText(std::string& out) {
    if (gPasted.empty()) {
        return false;
    }
    out.assign(gPasted);
    std::fill(gPasted.begin(), gPasted.end(), '\0');
    gPasted.clear();
    return true;
}

} // namespace vibe::platform

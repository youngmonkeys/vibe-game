#include "vibe/platform/Prefs.h"

#ifdef __EMSCRIPTEN__
#include <emscripten/emscripten.h>
#endif

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <nlohmann/json.hpp>
#include <string>

#include "vibe/core/AppInfo.h"

namespace vibe::platform {
namespace {

using Json = nlohmann::json;

constexpr const char* kFileName = "settings.json";

// SDL 3.4.18 on Emscripten: "/libsdl/<org>/<name>/". idbfs.js
// mounts IDBFS on "/libsdl" and loads it before main (research/dependencies.md).
std::string settingsPath() {
    char* dir = SDL_GetPrefPath(app_info::kOrg, app_info::kName);
    if (!dir) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: SDL_GetPrefPath failed: %s", SDL_GetError());
        return {};
    }
    std::string path = std::string(dir) + kFileName;
    SDL_free(dir);
    return path;
}

#ifdef __EMSCRIPTEN__
// Starts FS.syncfs(false) to copy the in-memory tree to IndexedDB. One sync
// runs at a time: a write during a sync asks for one more afterwards, because
// Emscripten logs console.error for two syncs in flight. idbfs.js sets
// Module.vibePrefsPersistent only when IndexedDB mounted and loaded. A failed
// sync warns once per page.
EM_JS(int, vibe_prefs_sync, (), {
    if (!Module.vibePrefsPersistent) {
        return 0;
    }
    var state = Module.vibePrefsSync || (Module.vibePrefsSync = { busy: false, again: false, warned: false });
    if (state.busy) {
        state.again = true;
        return 1;
    }
    var run = function() {
        state.busy = true;
        FS.syncfs(false, function(error) {
            state.busy = false;
            if (error && !state.warned) {
                state.warned = true;
                console.warn('WARN: prefs: saving to IndexedDB failed (' + error + '); settings last for this page only');
            }
            if (state.again) {
                state.again = false;
                run();
            }
        });
    };
    run();
    return 1;
});

constexpr const char* kSyncNote = "; syncing to IndexedDB";
#else
constexpr const char* kSyncNote = "";

int vibe_prefs_sync() {
    return 1;
}
#endif

bool readFlag(const Json& root, const char* key, bool fallback, bool& bad) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return fallback;
    }
    if (!it->is_boolean()) {
        bad = true;
        return fallback;
    }
    return it->get<bool>();
}

} // namespace

Settings loadSettings() {
    Settings settings;
    const std::string path = settingsPath();
    if (path.empty()) {
        return settings;
    }
    SDL_PathInfo info;
    if (!SDL_GetPathInfo(path.c_str(), &info)) {
        SDL_Log("prefs: no %s yet; music and sound effects on", path.c_str());
        return settings;
    }
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: cannot read %s: %s; using defaults", path.c_str(),
                    SDL_GetError());
        return settings;
    }
    const char* text = static_cast<const char*>(data);
    const Json root = Json::parse(text, text + size, nullptr, false);
    SDL_free(data);
    bool bad = !root.is_object();
    if (!bad) {
        settings.musicEnabled = readFlag(root, "musicEnabled", true, bad);
        settings.soundEffectEnabled = readFlag(root, "soundEffectEnabled", true, bad);
    }
    if (bad) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: %s is not valid settings JSON; using defaults for bad fields",
                    path.c_str());
    }
    SDL_Log("prefs: loaded %s: music %s, sound effects %s", path.c_str(), settings.musicEnabled ? "on" : "off",
            settings.soundEffectEnabled ? "on" : "off");
    return settings;
}

bool saveSettings(const Settings& settings) {
    const std::string path = settingsPath();
    if (path.empty()) {
        return false;
    }
    const std::string text = Json{{"musicEnabled", settings.musicEnabled},
                                  {"soundEffectEnabled", settings.soundEffectEnabled}}
                                 .dump();
    SDL_IOStream* io = SDL_IOFromFile(path.c_str(), "wb");
    if (!io) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: cannot open %s for writing: %s", path.c_str(),
                    SDL_GetError());
        return false;
    }
    const bool written = SDL_WriteIO(io, text.data(), text.size()) == text.size();
    if (!written) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: writing %s failed: %s", path.c_str(), SDL_GetError());
    }
    if (!SDL_CloseIO(io)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "prefs: closing %s failed: %s", path.c_str(), SDL_GetError());
        return false;
    }
    if (!written) {
        return false;
    }
    const bool persistent = vibe_prefs_sync() != 0;
    SDL_Log("prefs: saved music %s, sound effects %s%s", settings.musicEnabled ? "on" : "off",
            settings.soundEffectEnabled ? "on" : "off", persistent ? kSyncNote : " (this page only)");
    return true;
}

} // namespace vibe::platform

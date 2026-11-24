#include "vibe/platform/SessionStore.h"

#include <emscripten/emscripten.h>

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>
#include <cstring>
#include <string>

#include "vibe/core/AppInfo.h"

namespace vibe::platform {
namespace {

// The JSON text is built and read in JS, so the tokens never pass through a
// C++ JSON parser that might keep a copy. Each EM_JS returns 1 on success.
EM_JS(int, vibe_session_save, (const char* key, const char* access, const char* socket), {
    try {
        window.sessionStorage.setItem(UTF8ToString(key),
            JSON.stringify({accessToken: UTF8ToString(access), socketAccessToken: UTF8ToString(socket)}));
        return 1;
    } catch (e) {
        return 0;
    }
});

// `field` 0 is accessToken, 1 socketAccessToken. Returns a malloc'd UTF-8
// copy, or 0 when the key or the field is missing.
EM_JS(char*, vibe_session_load, (const char* key, int field), {
    try {
        var text = window.sessionStorage.getItem(UTF8ToString(key));
        if (!text) {
            return 0;
        }
        var value = JSON.parse(text)[field === 0 ? 'accessToken' : 'socketAccessToken'];
        if (typeof value !== 'string' || value.length === 0) {
            return 0;
        }
        return stringToNewUTF8(value);
    } catch (e) {
        return 0;
    }
});

EM_JS(int, vibe_session_clear, (const char* key), {
    try {
        window.sessionStorage.removeItem(UTF8ToString(key));
        return 1;
    } catch (e) {
        return 0;
    }
});

std::string storageKey() {
    return std::string(app_info::kId) + ".session";
}

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

bool take(int field, std::string& out) {
    char* text = vibe_session_load(storageKey().c_str(), field);
    if (!text) {
        return false;
    }
    out.assign(text);
    std::memset(text, 0, out.size());
    std::free(text);
    return true;
}

} // namespace

bool saveTokens(const std::string& accessToken, const std::string& socketAccessToken) {
    if (!vibe_session_save(storageKey().c_str(), accessToken.c_str(), socketAccessToken.c_str())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "session store: sessionStorage refused the tokens; a reload will need a new login");
        return false;
    }
    SDL_Log("session store: tokens saved in sessionStorage");
    return true;
}

bool loadTokens(StoredTokens& tokens) {
    if (!take(0, tokens.accessToken) || !take(1, tokens.socketAccessToken)) {
        wipe(tokens);
        return false;
    }
    return true;
}

void clearTokens() {
    if (vibe_session_clear(storageKey().c_str())) {
        SDL_Log("session store: tokens deleted from sessionStorage");
    } else {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session store: sessionStorage refused the delete");
    }
}

void wipe(StoredTokens& tokens) {
    wipe(tokens.accessToken);
    wipe(tokens.socketAccessToken);
}

} // namespace vibe::platform

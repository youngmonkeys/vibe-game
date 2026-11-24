#include "vibe/platform/SessionStore.h"

#include <SDL3/SDL_log.h>

#include <algorithm>

namespace vibe::platform {
namespace {

StoredTokens gTokens;

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

} // namespace

bool saveTokens(const std::string& accessToken, const std::string& socketAccessToken) {
    wipe(gTokens);
    gTokens.accessToken = accessToken;
    gTokens.socketAccessToken = socketAccessToken;
    SDL_Log("session store: tokens kept in memory until the app quits");
    return true;
}

bool loadTokens(StoredTokens& tokens) {
    if (gTokens.accessToken.empty() || gTokens.socketAccessToken.empty()) {
        wipe(tokens);
        return false;
    }
    tokens.accessToken = gTokens.accessToken;
    tokens.socketAccessToken = gTokens.socketAccessToken;
    return true;
}

void clearTokens() {
    wipe(gTokens);
    SDL_Log("session store: tokens deleted from memory");
}

void wipe(StoredTokens& tokens) {
    wipe(tokens.accessToken);
    wipe(tokens.socketAccessToken);
}

} // namespace vibe::platform

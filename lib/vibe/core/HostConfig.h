#pragma once

#include <string>
#include <string_view>

namespace vibe {

// Where the client finds the server (research/protocol.md, Local config).
// Read from /config/configs.json in the preload package.
struct HostConfig {
    std::string httpUrl;
    std::string masterUrl;
    std::string zoneName;
    std::string appName;
    std::string socketUrl;
    // socket.udpUsage, optional. Read only to say it is ignored: the web
    // client has no UDP, and every command uses the WebSocket (decision D15).
    bool udpUsage = false;
    int udpPort = 0;
    // Top-level `autoLogin`, optional, false when absent (plan 11). As in
    // Unity's LoginPresenter: start with the tokens a previous login stored.
    // On the web those live in sessionStorage, so this covers a reload of the
    // same tab only.
    bool autoLogin = false;
};

// Parses the text of configs.json. On failure returns false and sets `error`
// to one readable sentence. Never throws or aborts on bad input.
bool parseHostConfig(std::string_view text, HostConfig& config, std::string& error);

// Reads `path` with SDL_LoadFile and parses it.
bool loadHostConfig(const char* path, HostConfig& config, std::string& error);

// Logs one warning per URL that is not https:// or wss://. Release builds call
// it; plan 06 turns the warning into a refusal.
void warnInsecureUrls(const HostConfig& config);

} // namespace vibe

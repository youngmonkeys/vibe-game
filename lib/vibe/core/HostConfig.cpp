#include "vibe/core/HostConfig.h"

#include "vibe/core/AppInfo.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <cstdint>
#include <nlohmann/json.hpp>

namespace vibe {
namespace {

using Json = nlohmann::json;

#ifdef __EMSCRIPTEN__
constexpr const char* kSocketUrlKey = "webSocketUrl";
#else
constexpr const char* kSocketUrlKey = "tcpUrl";
#endif

bool readUrl(const Json& root, const char* key, bool required, std::string& out, std::string& error) {
    const auto it = root.find(key);
    if (it == root.end()) {
        if (required) {
            error = std::string("missing string field ") + key;
            return false;
        }
        return true;
    }
    if (!it->is_string() || it->get_ref<const std::string&>().empty()) {
        error = std::string("field ") + key + " is not a non-empty string";
        return false;
    }
    out = it->get<std::string>();
    while (out.size() > 1 && out.back() == '/') {
        out.pop_back();
    }
    return true;
}

bool readFlag(const Json& root, const char* key, bool& out, std::string& error) {
    const auto it = root.find(key);
    if (it == root.end()) {
        return true;
    }
    if (!it->is_boolean()) {
        error = std::string("field ") + key + " is not true or false";
        return false;
    }
    out = it->get<bool>();
    return true;
}

bool startsWith(std::string_view text, std::string_view prefix) {
    return text.substr(0, prefix.size()) == prefix;
}

} // namespace

bool parseHostConfig(std::string_view text, HostConfig& config, std::string& error) {
    // Exceptions are off, so use the non-throwing parse and check the result.
    const Json root = Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
    if (root.is_discarded()) {
        error = "not valid JSON";
        return false;
    }
    if (!root.is_object()) {
        error = "top level is not a JSON object";
        return false;
    }
    HostConfig parsed;
    parsed.zoneName = app_info::kZoneName;
    parsed.appName = app_info::kSocketAppName;
    if (!readUrl(root, "http", true, parsed.httpUrl, error) ||
        !readUrl(root, kSocketUrlKey, false, parsed.socketUrl, error) ||
        !readFlag(root, "udpUsage", parsed.udpUsage, error) || !readFlag(root, "autoLogin", parsed.autoLogin, error)) {
        return false;
    }
    if (const auto udpPort = root.find("udpPort"); udpPort != root.end()) {
        if (!udpPort->is_number_integer() || udpPort->get<std::int64_t>() < 1 ||
            udpPort->get<std::int64_t>() > 65535) {
            error = "field udpPort is not a port number 1-65535";
            return false;
        }
        parsed.udpPort = static_cast<int>(udpPort->get<std::int64_t>());
    }
    config = std::move(parsed);
    return true;
}

bool loadHostConfig(const char* path, HostConfig& config, std::string& error) {
    size_t size = 0;
    void* data = SDL_LoadFile(path, &size);
    if (!data) {
        error = std::string("cannot read ") + path + ": " + SDL_GetError();
        return false;
    }
    const bool ok = parseHostConfig(std::string_view(static_cast<const char*>(data), size), config, error);
    SDL_free(data);
    if (!ok) {
        error = std::string(path) + ": " + error;
    } else {
#ifdef __EMSCRIPTEN__
        if (config.udpUsage) {
            // Plan 08: logged once, at load.
            SDL_Log("config: udpUsage is true and ignored; every command, `s` included, uses the WebSocket");
        }
#else
        SDL_Log("config: socket transport %s", config.udpUsage ? "TCP and UDP" : "TCP");
#endif
    }
    return ok;
}

void warnInsecureUrls(const HostConfig& config) {
    if (!startsWith(config.httpUrl, "https://")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "config: http URL is not https:// (%s)", config.httpUrl.c_str());
    }
    if (!config.socketUrl.empty() && !startsWith(config.socketUrl, "wss://") &&
        !startsWith(config.socketUrl, "tcp://")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "config: socket URL is not encrypted (%s)",
                    config.socketUrl.c_str());
    }
}

} // namespace vibe

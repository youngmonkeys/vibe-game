#include "vibe/core/HostConfig.h"

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

// Finds `key` in `object` and checks it is a non-empty string.
bool readString(const Json& object, const char* path, const char* key, std::string& out,
                std::string& error) {
    const auto it = object.find(key);
    if (it == object.end() || !it->is_string() || it->get_ref<const std::string&>().empty()) {
        error = std::string("missing or empty string field ") + path + "." + key;
        return false;
    }
    out = it->get<std::string>();
    return true;
}

// Reads {"host": "...", "port": 8080, "hostOnly": false} into one base URL,
// the way the Unity client builds it (HttpUtil.cs): host alone when hostOnly
// is true, host:port otherwise.
bool readEndpoint(const Json& root, const char* name, std::string& url, std::string& error) {
    const auto it = root.find(name);
    if (it == root.end() || !it->is_object()) {
        error = std::string("missing object field ") + name;
        return false;
    }
    std::string host;
    if (!readString(*it, name, "host", host, error)) {
        return false;
    }
    bool hostOnly = false;
    if (const auto flag = it->find("hostOnly"); flag != it->end()) {
        if (!flag->is_boolean()) {
            error = std::string("field ") + name + ".hostOnly is not true or false";
            return false;
        }
        hostOnly = flag->get<bool>();
    }
    if (hostOnly) {
        url = host;
        return true;
    }
    const auto port = it->find("port");
    if (port == it->end() || !port->is_number_integer() || port->get<std::int64_t>() < 1 ||
        port->get<std::int64_t>() > 65535) {
        error = std::string("field ") + name + ".port is missing or not a port number 1-65535";
        return false;
    }
    url = host + ":" + std::to_string(port->get<std::int64_t>());
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
    if (!readEndpoint(root, "http", parsed.httpUrl, error) ||
        !readEndpoint(root, "master", parsed.masterUrl, error)) {
        return false;
    }
    const auto socket = root.find("socket");
    if (socket == root.end() || !socket->is_object()) {
        error = "missing object field socket";
        return false;
    }
    if (!readString(*socket, "socket", "zoneName", parsed.zoneName, error) ||
        !readString(*socket, "socket", "appName", parsed.appName, error) ||
        !readString(*socket, "socket", kSocketUrlKey, parsed.socketUrl, error)) {
        return false;
    }
    if (const auto udp = socket->find("udpUsage"); udp != socket->end()) {
        if (!udp->is_boolean()) {
            error = "field socket.udpUsage is not true or false";
            return false;
        }
        parsed.udpUsage = udp->get<bool>();
    }
    if (const auto udpPort = socket->find("udpPort"); udpPort != socket->end()) {
        if (!udpPort->is_number_integer() || udpPort->get<std::int64_t>() < 1 ||
            udpPort->get<std::int64_t>() > 65535) {
            error = "field socket.udpPort is not a port number 1-65535";
            return false;
        }
        parsed.udpPort = static_cast<int>(udpPort->get<std::int64_t>());
    }
    if (const auto autoLogin = root.find("autoLogin"); autoLogin != root.end()) {
        if (!autoLogin->is_boolean()) {
            error = "field autoLogin is not true or false";
            return false;
        }
        parsed.autoLogin = autoLogin->get<bool>();
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
            SDL_Log("config: socket.udpUsage is true and ignored; every command, `s` included, uses the WebSocket");
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
    if (!startsWith(config.masterUrl, "https://")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "config: master URL is not https:// (%s)", config.masterUrl.c_str());
    }
    if (!startsWith(config.socketUrl, "wss://") && !startsWith(config.socketUrl, "tcp://")) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "config: socket URL is not encrypted (%s)",
                    config.socketUrl.c_str());
    }
}

} // namespace vibe

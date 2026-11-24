#include "vibe/net/Url.h"

#include <cstdio>

namespace vibe::net {
namespace {

char lower(char c) {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

bool isSchemeChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' || c == '-' ||
           c == '.';
}

bool isHostChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '.' ||
           c == '_';
}

bool checkScheme(std::string_view text, const char* plain, const char* secure, bool requireSecure,
                 std::string& error) {
    ParsedUrl url;
    if (!parseUrl(text, url)) {
        error = "URL does not parse";
        return false;
    }
    if (url.scheme == secure) {
        return true;
    }
    if (url.scheme == plain && !requireSecure) {
        return true;
    }
    error = requireSecure ? std::string("URL scheme is ") + url.scheme + ", release builds need " + secure
                          : std::string("URL scheme is ") + url.scheme + ", expected " + plain + " or " + secure;
    return false;
}

} // namespace

bool parseUrl(std::string_view text, ParsedUrl& out) {
    for (const char c : text) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7f) {
            return false;
        }
    }
    const std::size_t schemeEnd = text.find("://");
    if (schemeEnd == std::string_view::npos || schemeEnd == 0) {
        return false;
    }
    ParsedUrl url;
    for (std::size_t i = 0; i < schemeEnd; ++i) {
        if (!isSchemeChar(text[i])) {
            return false;
        }
        url.scheme.push_back(lower(text[i]));
    }
    std::string_view rest = text.substr(schemeEnd + 3);
    const std::size_t authorityEnd = rest.find_first_of("/?#");
    const std::string_view authority = rest.substr(0, authorityEnd);
    if (authorityEnd != std::string_view::npos) {
        url.path = std::string(rest.substr(authorityEnd));
        if (url.path[0] != '/') {
            url.path.insert(url.path.begin(), '/');
        }
    }
    if (authority.find('@') != std::string_view::npos) {
        return false;
    }
    std::string_view hostPart = authority;
    std::string_view portPart;
    if (!authority.empty() && authority[0] == '[') {
        const std::size_t close = authority.find(']');
        if (close == std::string_view::npos || close == 1) {
            return false;
        }
        for (const char c : authority.substr(1, close - 1)) {
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F') || c == ':' ||
                  c == '.')) {
                return false;
            }
        }
        url.host = std::string(authority.substr(1, close - 1));
        const std::string_view after = authority.substr(close + 1);
        if (!after.empty()) {
            if (after[0] != ':') {
                return false;
            }
            portPart = after.substr(1);
            if (portPart.empty()) {
                return false;
            }
        }
    } else {
        const std::size_t colon = authority.find(':');
        if (colon != std::string_view::npos) {
            hostPart = authority.substr(0, colon);
            portPart = authority.substr(colon + 1);
            if (portPart.empty()) {
                return false;
            }
        }
        if (hostPart.empty()) {
            return false;
        }
        for (const char c : hostPart) {
            if (!isHostChar(c)) {
                return false;
            }
        }
        url.host = std::string(hostPart);
    }
    if (!portPart.empty()) {
        if (portPart.size() > 5) {
            return false;
        }
        int port = 0;
        for (const char c : portPart) {
            if (c < '0' || c > '9') {
                return false;
            }
            port = port * 10 + (c - '0');
        }
        if (port < 1 || port > 65535) {
            return false;
        }
        url.port = port;
    }
    out = std::move(url);
    return true;
}

bool checkWebSocketUrl(std::string_view text, bool requireSecure, std::string& error) {
    return checkScheme(text, "ws", "wss", requireSecure, error);
}

bool checkSocketUrl(std::string_view text, bool requireSecure, std::string& error) {
    ParsedUrl url;
    if (!parseUrl(text, url)) {
        error = "URL does not parse";
        return false;
    }
    if (url.scheme != "tcp") {
        return checkWebSocketUrl(text, requireSecure, error);
    }
    if (url.port == 0 || !url.path.empty()) {
        error = "a tcp:// URL needs a host and a port, and no path";
        return false;
    }
    return true;
}

std::string instanceTcpUrl(std::string_view host, long long tcpPort) {
    if (host.empty() || tcpPort <= 0 || tcpPort > 65535) {
        return {};
    }
    const bool ipv6 = host.find(':') != std::string_view::npos && host.front() != '[';
    std::string url = "tcp://";
    if (ipv6) {
        url += '[';
    }
    url.append(host);
    if (ipv6) {
        url += ']';
    }
    url += ':';
    url += std::to_string(tcpPort);
    ParsedUrl parsed;
    return parseUrl(url, parsed) ? url : std::string();
}

bool checkHttpUrl(std::string_view text, bool requireSecure, std::string& error) {
    return checkScheme(text, "http", "https", requireSecure, error);
}

bool checkLoginUrls(std::string_view httpUrl, std::string_view masterUrl, std::string_view socketUrl,
                    bool requireSecure, std::string& problem) {
    problem.clear();
    struct Item {
        const char* name;
        std::string_view url;
        bool webSocket;
    };
    const Item items[] = {{"http", httpUrl, false}, {"master", masterUrl, false}, {"WebSocket", socketUrl, true}};
    // First pass: anything that is wrong in every build.
    for (const Item& item : items) {
        std::string error;
        const bool ok = item.webSocket ? checkSocketUrl(item.url, false, error) : checkHttpUrl(item.url, false, error);
        if (!ok) {
            problem = std::string(item.name) + " URL: " + error;
            return false;
        }
    }
    // Second pass: insecure schemes.
    for (const Item& item : items) {
        std::string error;
        const bool ok = item.webSocket ? checkSocketUrl(item.url, true, error) : checkHttpUrl(item.url, true, error);
        if (!ok) {
            if (!problem.empty()) {
                problem += "; ";
            }
            problem += std::string(item.name) + " URL is not " + (item.webSocket ? "wss://" : "https://");
        }
    }
    return problem.empty() || !requireSecure;
}

std::string instanceWebSocketUrl(std::string_view allocationUrl) {
    ParsedUrl url;
    if (!parseUrl(allocationUrl, url) || (url.scheme != "ws" && url.scheme != "wss")) {
        return {};
    }
    std::string result(allocationUrl);
    if (url.path.empty()) {
        result += "/ws";
    } else if (url.path == "/") {
        result += "ws";
    }
    return result;
}

std::string urlOrigin(const ParsedUrl& url) {
    std::string origin = url.scheme + "://";
    origin += url.host.find(':') != std::string::npos ? "[" + url.host + "]" : url.host;
    if (url.port != 0) {
        origin += ":" + std::to_string(url.port);
    }
    return origin;
}

std::string formEncode(std::string_view value) {
    std::string out;
    out.reserve(value.size() * 3);
    for (const char ch : value) {
        const auto c = static_cast<unsigned char>(ch);
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~') {
            out.push_back(static_cast<char>(c));
        } else {
            char hex[4];
            std::snprintf(hex, sizeof hex, "%%%02X", c);
            out += hex;
        }
    }
    return out;
}

} // namespace vibe::net

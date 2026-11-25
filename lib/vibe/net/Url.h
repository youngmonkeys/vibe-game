#pragma once

#include <string>
#include <string_view>

namespace vibe::net {

// A URL the client is about to dial or fetch. Only what the client checks.
struct ParsedUrl {
    std::string scheme; // lower case: "http", "https", "ws", "wss", "tcp"
    std::string host;   // without brackets for IPv6
    int port = 0;       // 0 when the URL has none
    std::string path;   // starts with '/', or empty
};

// Parses `scheme://host[:port][/path][?query]`. Rejects user info, spaces,
// control characters, an empty host, and a port outside 1-65535. The query
// and fragment are kept in `path`.
bool parseUrl(std::string_view text, ParsedUrl& out);

// A URL the WebSocket may dial: ws:// or wss://, with a host. In a release
// build (`requireSecure`) only wss://. `error` gets one sentence that does not
// repeat the URL's query (which could hold a secret).
bool checkWebSocketUrl(std::string_view text, bool requireSecure, std::string& error);

bool checkSocketUrl(std::string_view text, bool requireSecure, std::string& error);

std::string instanceTcpUrl(std::string_view host, long long tcpPort);

// An HTTP base URL: http:// or https://, with a host; only https:// when
// `requireSecure`.
bool checkHttpUrl(std::string_view text, bool requireSecure, std::string& error);

// The URLs a login uses (plan 06 step 8). Every URL must parse with the
// right scheme family (the socket URL as checkSocketUrl, skipped when empty). With
// `requireSecure` (release builds), http://, ws:// or tcp://
// is refused: returns false and `problem` says which. Without it (dev builds),
// an insecure URL returns true and `problem` holds a warning for the log.
bool checkLoginUrls(std::string_view httpUrl, std::string_view masterUrl, std::string_view socketUrl,
                    bool requireSecure, std::string& problem);

// The allocation `websocketURL` has no path, and the server answers 404 on
// one without `/ws` (research/ezyfox-websocket.md). Returns the URL with
// `/ws` added when its path is empty, or "" when it does not parse.
std::string instanceWebSocketUrl(std::string_view allocationUrl);

// `scheme://host:port` only, for log lines.
std::string urlOrigin(const ParsedUrl& url);

// application/x-www-form-urlencoded value encoding.
std::string formEncode(std::string_view value);

} // namespace vibe::net

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace vibe::net {

// Plain structs that src/net/ decodes the protocol into (research/protocol.md).
// Nothing here knows about SDL, Emscripten, or the scene. Decoders check every
// field before one of these leaves src/net/: lengths, types, finite floats.

// Session command ids (research/protocol.md, Framing).
enum class Command : int {
    Error = 10,
    Handshake = 11,
    Ping = 12,
    Pong = 13,
    Disconnect = 14,
    Login = 20,
    LoginError = 21,
    AppAccess = 30,
    AppRequest = 31,
    AppExit = 33,
    AppAccessError = 34,
    AppRequestError = 35,
};

// DISCONNECT reasons the client acts on (EzyDisconnectReason in the JS client).
inline constexpr int kDisconnectAnotherSessionLogin = 3;

// Fixed-size text so a decoded message does not allocate per frame. Server
// player names are "USER#<id>"; display names are user text. A longer value
// is a malformed message, not a truncated one.
template <std::size_t Capacity>
struct FixedString {
    std::array<char, Capacity + 1> text{};
    std::uint16_t size = 0;

    static constexpr std::size_t capacity() { return Capacity; }
    bool assign(std::string_view value) {
        if (value.size() > Capacity) {
            return false;
        }
        for (std::size_t i = 0; i < value.size(); ++i) {
            text[i] = value[i];
        }
        size = static_cast<std::uint16_t>(value.size());
        text[size] = '\0';
        return true;
    }
    std::string_view view() const { return {text.data(), size}; }
    const char* c_str() const { return text.data(); }
    bool operator==(const FixedString& other) const { return view() == other.view(); }
};

inline constexpr std::size_t kMaxPlayerName = 64;
inline constexpr std::size_t kMaxDisplayName = 128;
using PlayerName = FixedString<kMaxPlayerName>;
using DisplayName = FixedString<kMaxDisplayName>;

enum class HttpStep : std::uint16_t {
    Login,
    GameServers,
    ServerInstances,
    Register,
    ForgotPassword,
};

inline constexpr std::uint16_t kFirstGameHttpStep = 100;

constexpr HttpStep gameHttpStep(std::uint16_t offset) {
    return static_cast<HttpStep>(kFirstGameHttpStep + offset);
}

constexpr bool isGameHttpStep(HttpStep step) {
    return static_cast<std::uint16_t>(step) >= kFirstGameHttpStep;
}

template <class... Messages>
struct MessageList {};

// ---- HTTP results -------------------------------------------------------

// POST /ezylogin/api/v1/login answered 2xx with ACTIVATED tokens. The tokens
// stay in memory only (plan 06 step 7) and are never logged.
struct HttpLoginOk {
    std::string accessToken;
    std::string socketAccessToken;
};

// POST /ezylogin/api/v1/login answered 2xx with a usable accessToken whose
// status is not ACTIVATED (plan 11). Unity opens
// {http}/redirect-proxy?accessToken=...&targetUri=register-success; the token
// is only for that URL. It is a secret like the others: never logged.
struct HttpLoginNotActivated {
    std::string accessToken;
};

// GET {master}/api/v1/game-master-server/game-servers answered 2xx: the game
// servers the player picks from after the HTTP login (RUNNING ones only).
struct HttpGameServerList {
    struct Server {
        std::int64_t id = 0;
        std::string displayName;
        std::string host;
        std::string version;
        std::string status;
    };
    std::vector<Server> servers;
};

// GET {master}/api/v1/game-master-server/game-servers/{id}/instances answered
// with at least one instance: the first one, which the socket login dials.
// `instanceWebSocketUrl` is its `websocketURL` with `/ws` added when the path
// is empty, or empty when it had none.
struct HttpServerInstance {
    std::int64_t instanceId = 0;
    std::string host;
    std::string instanceWebSocketUrl;
    std::string instanceTcpUrl;
    int udpPort = 0;
};

// POST /ezylogin/api/v1/anonymous/register (quick play). The password is a
// secret: it goes straight into the login form and is never logged.
struct HttpRegisterOk {
    std::string username;
    std::string password;
};

// The `pageToken` and `continuation` of an EzyPlatform pagination answer.
struct PageInfo {
    std::string nextPageToken; // "" when null
    bool hasNext = false;
    std::int64_t total = 0;
};

// POST {http}/ezylogin/api/v1/forgot-password answered 2xx (plan 11).
struct HttpForgotPasswordOk {};

// Any HTTP step that did not give a usable result. `status` 0 is a network
// error or the 15 s timeout. `reason` is safe to log: it never holds a token,
// a password, or a header.
struct HttpFailed {
    using Step = HttpStep;
    Step step = Step::Login;
    int status = 0;
    double elapsedMs = 0.0;
    std::string reason;
};

// What the player sees for a failed request: a short general message by
// status. `reason` holds the server's own text and goes to the log only.
inline const char* userMessage(const HttpFailed& failed) {
    if (failed.status == 0) {
        return "Cannot reach the server. Check your connection and try again.";
    }
    if (failed.step == HttpFailed::Step::Login && failed.status >= 400 && failed.status < 500) {
        return "Wrong user name or password.";
    }
    if (failed.status == 401 || failed.status == 403) {
        return "Your session has expired. Please log in again.";
    }
    if (failed.status == 404) {
        return "Not found. Please try again later.";
    }
    if (failed.status == 429) {
        return "Too many requests. Please wait a moment.";
    }
    if (failed.status >= 400 && failed.status < 500) {
        return "The request was refused.";
    }
    if (failed.status >= 500) {
        return "Internal server error. Please try again later.";
    }
    // A 2xx answer we could not read.
    return "The server sent an unexpected answer. Please try again.";
}

// ---- WebSocket events ---------------------------------------------------

struct SocketOpened {};
struct SocketError {};
struct SocketClosed {
    int code = 0;
    bool clean = false;
    FixedString<123> reason; // a close reason is at most 123 bytes
};

// ---- Session frames -----------------------------------------------------

// HANDSHAKE answer `[serverPublicKey, reconnectToken, sessionId, encryptedSessionKey]`.
// The reconnect token is a secret and is not kept here.
struct HandshakeOk {
    std::int64_t sessionId = 0;
};

// LOGIN answer `[zoneId, zoneName, userId, userName, data]`. `userName` is the
// in-zone name (USER#<id>), the `playerName` of every game command.
struct ZoneLoginOk {
    std::int32_t zoneId = 0;
    FixedString<64> zoneName;
    std::int64_t userId = 0;
    PlayerName userName;
};

// APP_ACCESS answer `[appId, appName, data]`.
struct AppAccessOk {
    std::int32_t appId = 0;
    FixedString<64> appName;
};

// ERROR, LOGIN_ERROR, APP_ACCESS_ERROR, APP_REQUEST_ERROR: `[code, message]`.
struct SessionError {
    Command command = Command::Error;
    std::int32_t code = 0;
    FixedString<128> message;
};

struct Pong {};

struct Disconnected {
    std::int32_t reason = 0;
};

// An app command the client does not decode.
struct OtherAppCommand {
    FixedString<32> command;
};

} // namespace vibe::net

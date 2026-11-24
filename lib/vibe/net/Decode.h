#pragma once

#include <cstddef>
#include <string_view>

#include "vibe/net/Messages.h"

namespace vibe::net {

// Decoders for every message the client reads. They never throw (exceptions
// are off) and never abort: bad input is a Malformed result with a short
// static reason, and the caller drops it with one rate-limited warning. They
// include no SDL or Emscripten header, so tests/net/ runs them under Node.

// A server text frame larger than this is dropped before it is parsed. The
// largest frame the client expects is getMMORoomPlayers with 20 players,
// about 3.5 KB (research/ezyfox-websocket.md, Probe log).
inline constexpr std::size_t kMaxInboundFrameBytes = 64 * 1024;
// Same idea for the login and allocation bodies, which are under 1 KB.
inline constexpr std::size_t kMaxHttpBodyBytes = 64 * 1024;

enum class DecodeStatus {
    Ok,        // `out` holds a message
    Ignored,   // well formed, nothing for the client (e.g. a command it does not use)
    Malformed, // dropped; `reason` says why
};

struct DecodeResult {
    DecodeStatus status = DecodeStatus::Malformed;
    // Static text, safe to log: it never quotes the payload.
    const char* reason = "";

    static DecodeResult ok() { return {DecodeStatus::Ok, ""}; }
    static DecodeResult ignored(const char* why) { return {DecodeStatus::Ignored, why}; }
    static DecodeResult malformed(const char* why) { return {DecodeStatus::Malformed, why}; }
};

// One WebSocket text frame, `[commandId, data]` or `[commandId]`.
DecodeResult decodeSessionFrame(std::string_view text, InboundMessage& out);

// POST /ezylogin/api/v1/login. `status` 0 is a network error or timeout.
// Always produces a message: HttpLoginOk, HttpLoginNotActivated (a usable
// token whose status is not ACTIVATED), or HttpFailed with a reason that holds
// only the server's error codes.
InboundMessage decodeLoginResponse(int status, std::string_view body, double elapsedMs);

// The game server list asks for `limit=50`; a longer list is malformed.
inline constexpr std::size_t kMaxGameServerItems = 50;

// GET {master}/api/v1/game-master-server/game-servers: HttpGameServerList or
// HttpFailed.
InboundMessage decodeGameServerListResponse(int status, std::string_view body, double elapsedMs);

// GET {master}/api/v1/game-master-server/game-servers/{id}/instances:
// HttpServerInstance (the first item) or HttpFailed, also when the list is
// empty.
InboundMessage decodeServerInstancesResponse(int status, std::string_view body, double elapsedMs);

// POST /ezylogin/api/v1/anonymous/register: HttpRegisterOk or HttpFailed.
InboundMessage decodeRegisterResponse(int status, std::string_view body, double elapsedMs);

// POST {http}/ezylogin/api/v1/forgot-password: HttpForgotPasswordOk for any
// 2xx (the body is not read), HttpFailed otherwise.
InboundMessage decodeForgotPasswordResponse(int status, std::string_view body, double elapsedMs);

// A page token goes into a URL query; a longer one is ignored.
inline constexpr std::size_t kMaxPageTokenBytes = 1024;

InboundMessage decodeHttpResponse(HttpStep step, int status, std::string_view body, double elapsedMs, bool firstPage);

const char* httpStepName(HttpStep step);

// Removes control characters and cuts `text` to at most `maxBytes` without
// splitting a UTF-8 character. For text other players wrote (plan 12).
std::string sanitizeShownText(std::string_view text, std::size_t maxBytes);

} // namespace vibe::net

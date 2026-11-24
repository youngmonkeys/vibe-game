#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include "vibe/net/Messages.h"

namespace vibe::net {

// Builders for the client's session frames, exactly as plan 17 recorded them
// (research/ezyfox-websocket.md, Session frames). Each returns the text of
// one WebSocket text frame. Never log the result of encodeZoneLogin: it holds
// the socket access token.

// The server closes the session with 1009 on a client frame above 2048 bytes.
inline constexpr std::size_t kMaxOutboundFrameBytes = 2048;

// [11,[clientId,"",clientType,clientVersion,false,""]]. Encryption stays off:
// the WebSocket does not offer it (plan 17).
std::string encodeHandshake(std::string_view clientId, std::string_view clientType,
                            std::string_view clientVersion);

// [20,[zone,"","",{"accessToken":socketAccessToken}]]
std::string encodeZoneLogin(std::string_view zoneName, std::string_view socketAccessToken);

// [30,[appName,null]]
std::string encodeAppAccess(std::string_view appName);

// [31,[appId,[command,data]]] where `dataJson` is already JSON text. Returns
// "" when `dataJson` does not parse.
std::string encodeAppRequest(int appId, std::string_view command, std::string_view dataJson);

// [12,[]]
std::string encodePing();

// Plan 11: the forgot-password HTTP body, `{"email":"..."}`, as Unity's
// ForgotPasswordRequest sends it. Invalid UTF-8 is replaced, never aborts.
std::string encodeForgotPasswordBody(std::string_view email);

} // namespace vibe::net

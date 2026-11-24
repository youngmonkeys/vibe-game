#include "vibe/net/Decode.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <string>

#include "vibe/net/DecodeSupport.h"
#include "vibe/net/Protocol.h"
#include "vibe/net/Url.h"

namespace vibe::net {

// Exceptions are off, so a throwing nlohmann accessor (`at`, `value`, a
// `get` of the wrong type) would abort the module. Every read below checks
// the type first and indexes only within checked bounds.

Json parseJson(std::string_view text) {
    return Json::parse(text.begin(), text.end(), nullptr, /*allow_exceptions=*/false);
}

bool readInt64(const Json& value, std::int64_t& out) {
    if (value.is_number_unsigned()) {
        const auto u = value.get<std::uint64_t>();
        if (u > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
            return false;
        }
        out = static_cast<std::int64_t>(u);
        return true;
    }
    if (value.is_number_integer()) {
        out = value.get<std::int64_t>();
        return true;
    }
    return false;
}

bool readInt32(const Json& value, std::int32_t& out) {
    std::int64_t wide = 0;
    if (!readInt64(value, wide) || wide < std::numeric_limits<std::int32_t>::min() ||
        wide > std::numeric_limits<std::int32_t>::max()) {
        return false;
    }
    out = static_cast<std::int32_t>(wide);
    return true;
}

// A JSON number that is finite once it is a float. 1e39 is a finite double
// but infinity as a float, so the range is checked before narrowing.
bool readFloat(const Json& value, float& out) {
    if (!value.is_number()) {
        return false;
    }
    const double wide = value.get<double>();
    if (!std::isfinite(wide) || std::fabs(wide) > static_cast<double>(FLT_MAX)) {
        return false;
    }
    out = static_cast<float>(wide);
    return std::isfinite(out);
}

bool hasControlChar(const std::string& text) {
    for (const char c : text) {
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7f) {
            return true;
        }
    }
    return false;
}

const Json* field(const Json& object, const char* key) {
    const auto it = object.find(key);
    return it == object.end() ? nullptr : &*it;
}

// A message is about 5 KB (RoomPlayers), and the wasm stack is 64 KB, so the
// decoders build in place in `out` rather than in a local copy.
DecodeResult drop(InboundMessage& out, const char* why) {
    out = std::monostate{};
    return DecodeResult::malformed(why);
}

// ---- HTTP ---------------------------------------------------------------

// Server error codes are short words ("incorrect", "invalid"). Anything else
// is left out of a log line, so an echoed input can never reach it.
bool isErrorWord(const std::string& text) {
    if (text.empty() || text.size() > 48) {
        return false;
    }
    for (const char c : text) {
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              c == '.' || c == '-')) {
            return false;
        }
    }
    return true;
}

// "password=incorrect, username=invalid" from a field-to-code map.
std::string errorSummary(const Json& body) {
    std::string summary;
    if (!body.is_object()) {
        return summary;
    }
    for (const auto& [key, value] : body.items()) {
        if (!value.is_string() || !isErrorWord(key) || !isErrorWord(value.get_ref<const std::string&>())) {
            continue;
        }
        if (!summary.empty()) {
            summary += ", ";
        }
        summary += key + "=" + value.get_ref<const std::string&>();
        if (summary.size() > 200) {
            break;
        }
    }
    return summary;
}

HttpFailed failure(HttpStep step, int status, double elapsedMs, std::string reason) {
    HttpFailed failed;
    failed.step = step;
    failed.status = status;
    failed.elapsedMs = elapsedMs;
    failed.reason = std::move(reason);
    return failed;
}

HttpFailed statusFailure(HttpStep step, int status, std::string_view body, double elapsedMs) {
    if (status == 0) {
        // Emscripten reports statusText "Not Found" here; it means nothing.
        return failure(step, status, elapsedMs, "network error or timeout");
    }
    std::string reason = "HTTP " + std::to_string(status);
    if (body.size() <= kMaxHttpBodyBytes) {
        const std::string summary = errorSummary(parseJson(body));
        if (!summary.empty()) {
            reason += ": " + summary;
        }
    }
    return failure(step, status, elapsedMs, std::move(reason));
}

// A token goes into a JSON string and a request header, so it must be
// printable ASCII with no quote or backslash.
bool readToken(const Json* value, std::string& out) {
    if (!value || !value->is_string()) {
        return false;
    }
    const auto& text = value->get_ref<const std::string&>();
    if (text.empty() || text.size() > 4096) {
        return false;
    }
    for (const char c : text) {
        if (c < 0x21 || c > 0x7e || c == '"' || c == '\\') {
            return false;
        }
    }
    out = text;
    return true;
}

// A server string for a text field: no control characters, at most `max`
// bytes. Null or absent gives "".
bool readOptionalText(const Json* value, std::size_t max, std::string& out) {
    out.clear();
    if (!value || value->is_null()) {
        return true;
    }
    if (!value->is_string()) {
        return false;
    }
    const auto& text = value->get_ref<const std::string&>();
    if (text.size() > max || hasControlChar(text)) {
        return false;
    }
    out = text;
    return true;
}

// The body of a 2xx answer as a JSON object, or a failure.
bool parseObjectBody(HttpStep step, int status, std::string_view body, double elapsedMs, const char* what,
                     Json& json, InboundMessage& failed) {
    if (body.size() > kMaxHttpBodyBytes) {
        failed = failure(step, status, elapsedMs, std::string(what) + " answer is too large");
        return false;
    }
    json = parseJson(body);
    if (json.is_discarded() || !json.is_object()) {
        failed = failure(step, status, elapsedMs, std::string(what) + " answer is not a JSON object");
        return false;
    }
    return true;
}

// A string field shown as text, sanitized; "" when absent or not a string.
std::string shownText(const Json& object, const char* key, std::size_t maxBytes) {
    const Json* value = field(object, key);
    return value && value->is_string() ? sanitizeShownText(value->get_ref<const std::string&>(), maxBytes) : "";
}

// `pageToken.next`, `continuation.hasNext` and `total`.
void readPage(const Json& json, PageInfo& page) {
    if (const Json* token = field(json, "pageToken"); token && token->is_object()) {
        const Json* next = field(*token, "next");
        if (next && next->is_string()) {
            const auto& text = next->get_ref<const std::string&>();
            // Base64 from the server; anything else would need escaping.
            bool plain = text.size() <= kMaxPageTokenBytes;
            for (const char c : text) {
                plain = plain && ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
                                  c == '+' || c == '/' || c == '=' || c == '-' || c == '_');
            }
            if (plain) {
                page.nextPageToken = text;
            }
        }
    }
    if (const Json* cont = field(json, "continuation"); cont && cont->is_object()) {
        const Json* hasNext = field(*cont, "hasNext");
        page.hasNext = hasNext && hasNext->is_boolean() && hasNext->get<bool>() && !page.nextPageToken.empty();
    }
    if (const Json* total = field(json, "total")) {
        if (!readInt64(*total, page.total) || page.total < 0) {
            page.total = 0;
        }
    }
}

// 6600.00 is a double in JSON; show whole numbers without the decimals.
std::string numberText(const Json& value) {
    std::int64_t whole = 0;
    if (readInt64(value, whole)) {
        return std::to_string(whole);
    }
    if (!value.is_number()) {
        return value.is_string() ? sanitizeShownText(value.get_ref<const std::string&>(), 32) : "";
    }
    const double d = value.get<double>();
    if (!std::isfinite(d) || std::fabs(d) > 1e15) {
        return "";
    }
    char text[48];
    if (d == std::floor(d)) {
        std::snprintf(text, sizeof text, "%.0f", d);
    } else {
        std::snprintf(text, sizeof text, "%.2f", d);
    }
    return text;
}

std::string sanitizeShownText(std::string_view text, std::size_t maxBytes) {
    std::string out;
    out.reserve(std::min(text.size(), maxBytes));
    for (const char c : text) {
        const auto u = static_cast<unsigned char>(c);
        if (u < 0x20 || u == 0x7f) {
            continue;
        }
        if (out.size() >= maxBytes) {
            break;
        }
        out.push_back(c);
    }
    // Never end inside a UTF-8 sequence (nlohmann gives valid UTF-8, so a cut
    // is the only way to get one).
    std::size_t n = 0;
    while (n < out.size() && n < 4 && (static_cast<unsigned char>(out[out.size() - 1 - n]) & 0xC0) == 0x80) {
        ++n;
    }
    if (n < out.size()) {
        const auto lead = static_cast<unsigned char>(out[out.size() - 1 - n]);
        if (lead >= 0xC0) {
            const std::size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : 2;
            if (n + 1 < want) {
                out.resize(out.size() - 1 - n);
            }
        }
    }
    return out;
}

namespace {

DecodeResult decodeAppRequest(const Protocol& protocol, const Json& data, InboundMessage& out) {
    // [appId, [command, data]]
    if (!data.is_array() || data.size() != 2 || !data[0].is_number_integer()) {
        return drop(out, "APP_REQUEST: not [appId, [command, data]]");
    }
    const Json& inner = data[1];
    if (!inner.is_array() || inner.empty() || inner.size() > 2 || !inner[0].is_string()) {
        return drop(out, "APP_REQUEST: not [command, data]");
    }
    const auto& command = inner[0].get_ref<const std::string&>();
    static const Json kNull;
    const Json& payload = inner.size() == 2 ? inner[1] : kNull;
    if (const Protocol::CommandDecoder* decoder = protocol.findCommand(command)) {
        return (*decoder)(payload, out);
    }
    auto& other = out.emplace<OtherAppCommand>();
    if (hasControlChar(command) || !other.command.assign(command)) {
        return drop(out, "APP_REQUEST: bad command name");
    }
    return DecodeResult::ok();
}

DecodeResult decodeError(Command command, const Json& data, InboundMessage& out) {
    auto& msg = out.emplace<SessionError>();
    msg.command = command;
    if (data.is_array() && !data.empty()) {
        if (!readInt32(data[0], msg.code)) {
            return drop(out, "error frame: bad code");
        }
        if (data.size() > 1 && data[1].is_string()) {
            const auto& text = data[1].get_ref<const std::string&>();
            // A message too long for the struct is left out, not cut inside
            // a UTF-8 sequence.
            if (!hasControlChar(text)) {
                msg.message.assign(text);
            }
        }
    } else if (!readInt32(data, msg.code) && !data.is_null()) {
        return drop(out, "error frame: not [code, message]");
    }
    return DecodeResult::ok();
}

} // namespace

DecodeResult decodeSessionFrame(const Protocol& protocol, std::string_view text, InboundMessage& out) {
    if (text.size() > kMaxInboundFrameBytes) {
        return drop(out, "frame larger than the inbound cap");
    }
    const Json frame = parseJson(text);
    if (frame.is_discarded()) {
        return drop(out, "frame is not JSON");
    }
    // [commandId, data], or [commandId] for PONG and other no-data commands.
    if (!frame.is_array() || frame.empty() || frame.size() > 2 || !frame[0].is_number_integer()) {
        return drop(out, "frame is not [command, data]");
    }
    std::int32_t id = 0;
    if (!readInt32(frame[0], id)) {
        return drop(out, "frame command id out of range");
    }
    static const Json kNull;
    const Json& data = frame.size() == 2 ? frame[1] : kNull;
    const auto command = static_cast<Command>(id);
    switch (command) {
    case Command::Handshake: {
        // [serverPublicKey, reconnectToken, sessionId, encryptedSessionKey]
        if (!data.is_array() || data.size() < 3) {
            return drop(out, "HANDSHAKE: not an array of 3 or more");
        }
        auto& msg = out.emplace<HandshakeOk>();
        if (!data[2].is_null() && !readInt64(data[2], msg.sessionId)) {
            return drop(out, "HANDSHAKE: bad session id");
        }
        return DecodeResult::ok();
    }
    case Command::Login: {
        // [zoneId, zoneName, userId, userName, data]
        if (!data.is_array() || data.size() < 4) {
            return drop(out, "LOGIN: not an array of 4 or more");
        }
        auto& msg = out.emplace<ZoneLoginOk>();
        if (!readInt32(data[0], msg.zoneId) || !readName(data[1], msg.zoneName) ||
            !readInt64(data[2], msg.userId) || !readName(data[3], msg.userName)) {
            return drop(out, "LOGIN: bad zone or user");
        }
        return DecodeResult::ok();
    }
    case Command::AppAccess: {
        // [appId, appName, data]
        if (!data.is_array() || data.size() < 2) {
            return drop(out, "APP_ACCESS: not an array of 2 or more");
        }
        auto& msg = out.emplace<AppAccessOk>();
        if (!readInt32(data[0], msg.appId) || !readName(data[1], msg.appName)) {
            return drop(out, "APP_ACCESS: bad app id or name");
        }
        return DecodeResult::ok();
    }
    case Command::AppRequest:
        return decodeAppRequest(protocol, data, out);
    case Command::Pong:
        out = Pong{};
        return DecodeResult::ok();
    case Command::Disconnect: {
        auto& msg = out.emplace<Disconnected>();
        if (data.is_array() && !data.empty()) {
            if (!readInt32(data[0], msg.reason)) {
                return drop(out, "DISCONNECT: bad reason");
            }
        } else if (!data.is_null() && !readInt32(data, msg.reason)) {
            return drop(out, "DISCONNECT: bad reason");
        }
        return DecodeResult::ok();
    }
    case Command::Error:
    case Command::LoginError:
    case Command::AppAccessError:
    case Command::AppRequestError:
        return decodeError(command, data, out);
    default:
        return DecodeResult::ignored("command the client does not use");
    }
}

InboundMessage decodeLoginResponse(int status, std::string_view body, double elapsedMs) {
    constexpr auto step = HttpFailed::Step::Login;
    if (status < 200 || status > 299) {
        return statusFailure(step, status, body, elapsedMs);
    }
    if (body.size() > kMaxHttpBodyBytes) {
        return failure(step, status, elapsedMs, "login answer is too large");
    }
    const Json json = parseJson(body);
    if (json.is_discarded() || !json.is_object()) {
        return failure(step, status, elapsedMs, "login answer is not a JSON object");
    }
    const Json* tokenStatus = field(json, "accessTokenStatus");
    if (!tokenStatus || !tokenStatus->is_string() || tokenStatus->get_ref<const std::string&>() != "ACTIVATED") {
        // Unity opens the register-success page with this token (plan 11).
        HttpLoginNotActivated notActivated;
        if (tokenStatus && readToken(field(json, "accessToken"), notActivated.accessToken)) {
            return notActivated;
        }
        std::string reason = "account is not activated";
        if (tokenStatus && tokenStatus->is_string() && isErrorWord(tokenStatus->get_ref<const std::string&>())) {
            reason += " (" + tokenStatus->get_ref<const std::string&>() + ")";
        }
        return failure(step, status, elapsedMs, std::move(reason));
    }
    HttpLoginOk ok;
    if (!readToken(field(json, "accessToken"), ok.accessToken)) {
        return failure(step, status, elapsedMs, "login answer has no usable accessToken");
    }
    // socketAccessToken falls back to accessToken when null (research/protocol.md).
    const Json* socketToken = field(json, "socketAccessToken");
    if (!socketToken || socketToken->is_null()) {
        ok.socketAccessToken = ok.accessToken;
    } else if (!readToken(socketToken, ok.socketAccessToken)) {
        return failure(step, status, elapsedMs, "login answer has a bad socketAccessToken");
    }
    return ok;
}

InboundMessage decodeGameServerListResponse(int status, std::string_view body, double elapsedMs) {
    constexpr auto step = HttpFailed::Step::GameServers;
    if (status < 200 || status > 299) {
        return statusFailure(step, status, body, elapsedMs);
    }
    Json json;
    InboundMessage failed;
    if (!parseObjectBody(step, status, body, elapsedMs, "game server list", json, failed)) {
        return failed;
    }
    const Json* items = field(json, "items");
    if (!items || !items->is_array()) {
        return failure(step, status, elapsedMs, "game server list has no items array");
    }
    if (items->size() > kMaxGameServerItems) {
        return failure(step, status, elapsedMs, "game server list has more than 50 items");
    }
    HttpGameServerList list;
    list.servers.reserve(items->size());
    for (const Json& item : *items) {
        if (!item.is_object()) {
            return failure(step, status, elapsedMs, "game server item is not an object");
        }
        HttpGameServerList::Server server;
        const Json* id = field(item, "id");
        if (!id || !readInt64(*id, server.id) || server.id <= 0) {
            return failure(step, status, elapsedMs, "game server item has a bad id");
        }
        if (!readOptionalText(field(item, "displayName"), 128, server.displayName) ||
            !readOptionalText(field(item, "host"), 253, server.host) ||
            !readOptionalText(field(item, "version"), 64, server.version) ||
            !readOptionalText(field(item, "status"), 32, server.status)) {
            return failure(step, status, elapsedMs, "game server item has a bad text field");
        }
        list.servers.push_back(std::move(server));
    }
    return list;
}

InboundMessage decodeServerInstancesResponse(int status, std::string_view body, double elapsedMs) {
    constexpr auto step = HttpFailed::Step::ServerInstances;
    if (status < 200 || status > 299) {
        return statusFailure(step, status, body, elapsedMs);
    }
    Json json;
    InboundMessage failed;
    if (!parseObjectBody(step, status, body, elapsedMs, "instance list", json, failed)) {
        return failed;
    }
    const Json* items = field(json, "items");
    if (!items || !items->is_array()) {
        return failure(step, status, elapsedMs, "instance list has no items array");
    }
    if (items->empty()) {
        return failure(step, status, elapsedMs, "the game server has no available instance");
    }
    const Json& item = items->front();
    if (!item.is_object()) {
        return failure(step, status, elapsedMs, "instance item is not an object");
    }
    HttpServerInstance instance;
    const Json* id = field(item, "id");
    if (!id || !readInt64(*id, instance.instanceId)) {
        return failure(step, status, elapsedMs, "instance item has a bad id");
    }
    if (!readOptionalText(field(item, "host"), 253, instance.host)) {
        return failure(step, status, elapsedMs, "instance item has a bad host");
    }
    std::string url;
    if (!readOptionalText(field(item, "websocketURL"), 512, url)) {
        return failure(step, status, elapsedMs, "instance websocketURL is not a string");
    }
    if (!url.empty()) {
        instance.instanceWebSocketUrl = instanceWebSocketUrl(url);
        if (instance.instanceWebSocketUrl.empty()) {
            return failure(step, status, elapsedMs, "instance websocketURL is not a ws:// or wss:// URL");
        }
    }
    const Json* tcpPort = field(item, "tcpPort");
    std::int64_t port = 0;
    if (tcpPort && !tcpPort->is_null() && !readInt64(*tcpPort, port)) {
        return failure(step, status, elapsedMs, "instance tcpPort is not a number");
    }
    instance.instanceTcpUrl = instanceTcpUrl(instance.host, port);
    const Json* udpPort = field(item, "udpPort");
    std::int64_t udp = 0;
    if (udpPort && !udpPort->is_null() && !readInt64(*udpPort, udp)) {
        return failure(step, status, elapsedMs, "instance udpPort is not a number");
    }
    instance.udpPort = udp > 0 && udp <= 65535 ? static_cast<int>(udp) : 0;
    return instance;
}

InboundMessage decodeRegisterResponse(int status, std::string_view body, double elapsedMs) {
    constexpr auto step = HttpFailed::Step::Register;
    if (status < 200 || status > 299) {
        return statusFailure(step, status, body, elapsedMs);
    }
    Json json;
    InboundMessage failed;
    if (!parseObjectBody(step, status, body, elapsedMs, "register", json, failed)) {
        return failed;
    }
    HttpRegisterOk ok;
    // Both go into a form body, which encodes anything; still refuse control
    // characters and absurd sizes.
    if (!readOptionalText(field(json, "username"), 256, ok.username) || ok.username.empty()) {
        return failure(step, status, elapsedMs, "register answer has no usable username");
    }
    if (!readOptionalText(field(json, "password"), 256, ok.password) || ok.password.empty()) {
        return failure(step, status, elapsedMs, "register answer has no usable password");
    }
    return ok;
}

InboundMessage decodeForgotPasswordResponse(int status, std::string_view body, double elapsedMs) {
    constexpr auto step = HttpFailed::Step::ForgotPassword;
    if (status < 200 || status > 299) {
        return statusFailure(step, status, body, elapsedMs);
    }
    return HttpForgotPasswordOk{};
}

InboundMessage decodeHttpResponse(const Protocol& protocol, HttpStep step, int status, std::string_view body,
                                  double elapsedMs, bool firstPage) {
    switch (step) {
    case HttpStep::Login: return decodeLoginResponse(status, body, elapsedMs);
    case HttpStep::GameServers: return decodeGameServerListResponse(status, body, elapsedMs);
    case HttpStep::ServerInstances: return decodeServerInstancesResponse(status, body, elapsedMs);
    case HttpStep::Register: return decodeRegisterResponse(status, body, elapsedMs);
    case HttpStep::ForgotPassword: return decodeForgotPasswordResponse(status, body, elapsedMs);
    }
    if (const Protocol::HttpDecoder* decoder = protocol.findHttp(step)) {
        return (*decoder)(HttpAnswer{step, status, body, elapsedMs, firstPage});
    }
    return failure(step, status, elapsedMs, "no decoder for this HTTP step");
}

const char* httpStepName(const Protocol& protocol, HttpStep step) {
    switch (step) {
    case HttpStep::Login: return "login";
    case HttpStep::GameServers: return "game servers";
    case HttpStep::ServerInstances: return "server instances";
    case HttpStep::Register: return "register";
    case HttpStep::ForgotPassword: return "forgot password";
    }
    return protocol.httpName(step);
}

} // namespace vibe::net

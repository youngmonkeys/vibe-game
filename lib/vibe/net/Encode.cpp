#include "vibe/net/Encode.h"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <nlohmann/json.hpp>

namespace vibe::net {
namespace {

using Json = nlohmann::json;

std::string dumpFrame(int command, Json data) {
    // `replace` keeps dump() from aborting on invalid UTF-8 (exceptions are off).
    return Json::array({command, std::move(data)}).dump(-1, ' ', false, Json::error_handler_t::replace);
}

std::string str(std::string_view text) {
    return std::string(text);
}

} // namespace

std::string encodeHandshake(std::string_view clientId, std::string_view clientType,
                            std::string_view clientVersion) {
    return dumpFrame(11, Json::array({str(clientId), "", str(clientType), str(clientVersion), false, ""}));
}

std::string encodeZoneLogin(std::string_view zoneName, std::string_view socketAccessToken) {
    Json data = Json::object();
    data["accessToken"] = str(socketAccessToken);
    return dumpFrame(20, Json::array({str(zoneName), "", "", std::move(data)}));
}

std::string encodeAppAccess(std::string_view appName) {
    return dumpFrame(30, Json::array({str(appName), nullptr}));
}

std::string encodeAppRequest(int appId, std::string_view command, std::string_view dataJson) {
    Json data = Json::parse(dataJson.begin(), dataJson.end(), nullptr, /*allow_exceptions=*/false);
    if (data.is_discarded()) {
        return {};
    }
    return dumpFrame(31, Json::array({appId, Json::array({str(command), std::move(data)})}));
}

std::string encodePing() {
    return "[12,[]]";
}

std::string encodeForgotPasswordBody(std::string_view email) {
    Json body = Json::object();
    body["email"] = str(email);
    return body.dump(-1, ' ', false, Json::error_handler_t::replace);
}

} // namespace vibe::net

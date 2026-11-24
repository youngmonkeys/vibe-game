#pragma once

#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>
#include <string_view>

#include "vibe/net/Decode.h"
#include "vibe/net/Messages.h"

namespace vibe::net {

using Json = nlohmann::json;

Json parseJson(std::string_view text);
bool readInt64(const Json& value, std::int64_t& out);
bool readInt32(const Json& value, std::int32_t& out);
bool readFloat(const Json& value, float& out);
bool hasControlChar(const std::string& text);

// A non-empty string without control characters that fits `out`. JSON
// strings are already valid UTF-8 once nlohmann accepts them.
template <std::size_t N>
bool readName(const Json& value, FixedString<N>& out) {
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    return !text.empty() && !hasControlChar(text) && out.assign(text);
}

// Like readName, but an empty string or null is allowed (gives "").
template <std::size_t N>
bool readOptionalName(const Json& value, FixedString<N>& out) {
    if (value.is_null()) {
        out.assign({});
        return true;
    }
    if (!value.is_string()) {
        return false;
    }
    const auto& text = value.get_ref<const std::string&>();
    return !hasControlChar(text) && out.assign(text);
}

const Json* field(const Json& object, const char* key);
DecodeResult drop(InboundMessage& out, const char* why);

bool isErrorWord(const std::string& text);
std::string errorSummary(const Json& body);
HttpFailed failure(HttpStep step, int status, double elapsedMs, std::string reason);
HttpFailed statusFailure(HttpStep step, int status, std::string_view body, double elapsedMs);
bool readToken(const Json* value, std::string& out);
bool readOptionalText(const Json* value, std::size_t max, std::string& out);
bool parseObjectBody(HttpStep step, int status, std::string_view body, double elapsedMs, const char* what, Json& json,
                     InboundMessage& failed);
std::string shownText(const Json& object, const char* key, std::size_t maxBytes);
void readPage(const Json& json, PageInfo& page);
std::string numberText(const Json& value);

} // namespace vibe::net

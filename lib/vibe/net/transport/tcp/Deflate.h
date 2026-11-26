#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace vibe::net {

bool deflateRaw(std::string_view input, std::string& out);

bool inflateRaw(std::string_view input, std::size_t maxBytes, std::string& out, std::string& error);

} // namespace vibe::net

#include "vibe/net/Protocol.h"

namespace vibe::net {

Protocol::Protocol() : streams_(kInboundMessageCount) {}

Protocol& Protocol::command(std::string_view name, CommandDecoder decoder) {
    commands_.insert_or_assign(std::string(name), std::move(decoder));
    return *this;
}

Protocol& Protocol::http(HttpStep step, std::string_view name, HttpDecoder decoder) {
    http_.insert_or_assign(step, HttpEntry{std::string(name), std::move(decoder)});
    return *this;
}

const Protocol::CommandDecoder* Protocol::findCommand(std::string_view name) const {
    const auto it = commands_.find(name);
    return it == commands_.end() ? nullptr : &it->second;
}

const Protocol::HttpDecoder* Protocol::findHttp(HttpStep step) const {
    const auto it = http_.find(step);
    return it == http_.end() ? nullptr : &it->second.decoder;
}

const char* Protocol::httpName(HttpStep step) const {
    const auto it = http_.find(step);
    return it == http_.end() ? "game" : it->second.name.c_str();
}

bool Protocol::isStream(const InboundMessage& message) const {
    const std::size_t index = message.index();
    return index < streams_.size() && streams_[index];
}

bool Protocol::coalesce(InboundMessage& queued, const InboundMessage& incoming) const {
    const std::size_t index = incoming.index();
    return index < streams_.size() && streams_[index] && streams_[index](queued, incoming);
}

} // namespace vibe::net

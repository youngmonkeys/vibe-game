#pragma once

#include <cstddef>
#include <functional>
#include <map>
#include <nlohmann/json_fwd.hpp>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "vibe/net/Decode.h"
#include "vibe/net/Messages.h"

namespace vibe::net {

struct HttpAnswer {
    HttpStep step = HttpStep::Login;
    int status = 0;
    std::string_view body;
    double elapsedMs = 0.0;
    bool firstPage = true;
};

class Protocol {
public:
    using CommandDecoder = std::function<DecodeResult(const nlohmann::json& payload, InboundMessage& out)>;
    using HttpDecoder = std::function<InboundMessage(const HttpAnswer& answer)>;
    using Coalescer = std::function<bool(InboundMessage& queued, const InboundMessage& incoming)>;

    Protocol();

    Protocol& command(std::string_view name, CommandDecoder decoder);

    template <class Message, class Decode>
    Protocol& command(std::string_view name, Decode decode) {
        static_assert(isInboundMessage<Message>, "the message is not in GameAppMessages");
        return command(name, CommandDecoder([decode = std::move(decode)](const nlohmann::json& payload,
                                                                         InboundMessage& out) {
            Message& message = out.template emplace<Message>();
            const DecodeResult result = decode(payload, message);
            if (result.status != DecodeStatus::Ok) {
                out = std::monostate{};
            }
            return result;
        }));
    }

    Protocol& http(HttpStep step, std::string_view name, HttpDecoder decoder);

    template <class Message, class Merge>
    Protocol& stream(Merge merge) {
        static_assert(isInboundMessage<Message>, "the message is not in InboundMessage");
        streams_[messageIndex<Message>] = [merge = std::move(merge)](InboundMessage& queued,
                                                                     const InboundMessage& incoming) {
            auto* older = std::get_if<Message>(&queued);
            const auto* newer = std::get_if<Message>(&incoming);
            return older && newer && merge(*older, *newer);
        };
        return *this;
    }

    const CommandDecoder* findCommand(std::string_view name) const;
    const HttpDecoder* findHttp(HttpStep step) const;
    const char* httpName(HttpStep step) const;
    bool isStream(const InboundMessage& message) const;
    bool coalesce(InboundMessage& queued, const InboundMessage& incoming) const;

private:
    struct HttpEntry {
        std::string name;
        HttpDecoder decoder;
    };

    std::map<std::string, CommandDecoder, std::less<>> commands_;
    std::map<HttpStep, HttpEntry> http_;
    std::vector<Coalescer> streams_;
};

} // namespace vibe::net

#pragma once

#include <cstddef>
#include <type_traits>
#include <variant>

#include "vibe/net/CoreMessages.h"
#include "net/GameProtocol.h"

namespace vibe::net {

using CoreMessages =
    MessageList<std::monostate, HttpLoginOk, HttpLoginNotActivated, HttpGameServerList, HttpServerInstance,
                HttpRegisterOk, HttpForgotPasswordOk, HttpFailed, SocketOpened, SocketError, SocketClosed, HandshakeOk,
                ZoneLoginOk, AppAccessOk, SessionError, Pong, Disconnected, OtherAppCommand>;

template <class Core, class Http, class App>
struct JoinMessageLists;

template <class... Core, class... Http, class... App>
struct JoinMessageLists<MessageList<Core...>, MessageList<Http...>, MessageList<App...>> {
    using Variant = std::variant<Core..., Http..., App...>;
    static constexpr std::size_t kFirstHttp = sizeof...(Core);
    static constexpr std::size_t kFirstApp = sizeof...(Core) + sizeof...(Http);
};

using InboundMessages = JoinMessageLists<CoreMessages, GameHttpMessages, GameAppMessages>;
using InboundMessage = InboundMessages::Variant;

inline constexpr std::size_t kInboundMessageCount = std::variant_size_v<InboundMessage>;

template <class Message, class Variant>
struct MessageIndex;

template <class Message, class... Types>
struct MessageIndex<Message, std::variant<Types...>> {
    static constexpr std::size_t find() {
        constexpr bool matches[] = {std::is_same_v<Message, Types>...};
        for (std::size_t i = 0; i < sizeof...(Types); ++i) {
            if (matches[i]) {
                return i;
            }
        }
        return sizeof...(Types);
    }
    static constexpr std::size_t value = find();
};

template <class Message>
inline constexpr std::size_t messageIndex = MessageIndex<Message, InboundMessage>::value;

template <class Message>
inline constexpr bool isInboundMessage = messageIndex<Message> < kInboundMessageCount;

inline bool isGameHttpResult(const InboundMessage& message) {
    return message.index() >= InboundMessages::kFirstHttp && message.index() < InboundMessages::kFirstApp;
}

inline bool isGameAppMessage(const InboundMessage& message) {
    return message.index() >= InboundMessages::kFirstApp && message.index() != std::variant_npos;
}

} // namespace vibe::net

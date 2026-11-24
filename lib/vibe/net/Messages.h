#pragma once

#include <cstddef>
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

inline bool isGameHttpResult(const InboundMessage& message) {
    return message.index() >= InboundMessages::kFirstHttp && message.index() < InboundMessages::kFirstApp;
}

inline bool isGameAppMessage(const InboundMessage& message) {
    return message.index() >= InboundMessages::kFirstApp && message.index() != std::variant_npos;
}

} // namespace vibe::net

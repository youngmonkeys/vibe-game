#pragma once

#include <memory>

#ifdef __EMSCRIPTEN__
#include "vibe/net/transport/WebSocket.h"
#else
#include "vibe/net/transport/TcpSocket.h"
#include "vibe/net/transport/UdpTcpSocket.h"
#endif

namespace vibe::net {

#ifdef __EMSCRIPTEN__
using SocketTransport = WebSocketTransport;
#else
using SocketTransport = TcpTransport;
#endif

inline std::unique_ptr<SocketTransport> newSocketTransport(InboundQueue& inbound, bool udpUsage) {
#ifdef __EMSCRIPTEN__
    (void)udpUsage;
    return std::make_unique<WebSocketTransport>(inbound);
#else
    if (udpUsage) {
        return std::make_unique<UdpTcpTransport>(inbound);
    }
    return std::make_unique<TcpTransport>(inbound);
#endif
}

} // namespace vibe::net

#pragma once

#include <memory>

#include "vibe/core/HostConfig.h"

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

inline std::unique_ptr<SocketTransport> newSocketTransport(InboundQueue& inbound, bool udpUsage,
                                                          const TcpOptions& tcp) {
#ifdef __EMSCRIPTEN__
    (void)udpUsage;
    (void)tcp;
    return std::make_unique<WebSocketTransport>(inbound);
#else
    if (udpUsage) {
        return std::make_unique<UdpTcpTransport>(inbound, tcp);
    }
    return std::make_unique<TcpTransport>(inbound, tcp);
#endif
}

} // namespace vibe::net

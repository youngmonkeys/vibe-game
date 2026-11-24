#pragma once

#include <string>

#include "vibe/net/Queues.h"
#include "vibe/net/WarnLimiter.h"
#include "vibe/net/transport/Traffic.h"

namespace vibe::net {

// The EzyFox session socket on emscripten/websocket.h: JSON text frames
// (research/ezyfox-websocket.md). The browser calls the callbacks on the main
// thread between frames. They decode with src/net/Decode.h and push into the
// inbound queue. They never touch scene state.
class WebSocketTransport {
public:
    explicit WebSocketTransport(InboundQueue& inbound);
    ~WebSocketTransport();
    WebSocketTransport(const WebSocketTransport&) = delete;
    WebSocketTransport& operator=(const WebSocketTransport&) = delete;

    // Starts dialing. SocketOpened, or SocketError then SocketClosed, arrive
    // in the inbound queue. Returns false if the browser refused the URL at
    // once (no events follow). Closes any socket that is already open.
    bool open(const std::string& url);

    // Sends one text frame now. False when the socket is not open or the
    // browser refused the send.
    bool send(const std::string& text);

    // Starts a close handshake. SocketClosed follows in the queue.
    void close(unsigned short code, const char* reason);

    // Drops the socket without waiting for its close event. No more events
    // from it reach the queue.
    void abandon();

    // EzyFox PING on a browser timer, not on the frame, so a hidden tab
    // keeps its session (docs/architecture.md, Network callbacks).
    void startPing(int periodMs);
    void stopPing();

    bool isOpen() const { return open_; }
    const TrafficCounters& traffic() const { return traffic_; }

    // Called by the socket callbacks only.
    void onOpen(int socket);
    void onMessage(int socket, const unsigned char* data, unsigned numBytes, bool isText);
    void onError(int socket);
    void onClose(int socket, int code, bool clean, const char* reason);
    void onPingTimer();

private:
    InboundQueue& inbound_;
    int socket_ = 0;
    bool open_ = false;
    long pingTimer_ = 0;
    bool warnedBinary_ = false;
    TrafficCounters traffic_;
    WarnLimiter malformed_;
};

} // namespace vibe::net

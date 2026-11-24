#include "vibe/net/transport/WebSocket.h"

#include <emscripten/emscripten.h>
#include <emscripten/websocket.h>

#include <SDL3/SDL_log.h>

#include <string_view>
#include <variant>

#include "vibe/net/Decode.h"
#include "vibe/net/Encode.h"

namespace vibe::net {
namespace {

// Live transports. When the last one goes, the event buffer goes too.
int gLiveTransports = 0;

// Emscripten's WebSocket library (libwebsocket.js, WS.getSocketEvent) mallocs
// one 520-byte event buffer on the first socket event, keeps the pointer only
// in JavaScript, and never frees it. web-asan's LeakSanitizer reports it when
// the runtime exits, for example when the page closes from the lounge
// (plan 07). getSocketEvent allocates it again if a later socket needs it.
EM_JS(void, vibe_free_websocket_event_buffer, (), {
    if (typeof WS !== 'undefined' && WS.socketEvent) {
        _free(WS.socketEvent);
        WS.socketEvent = null;
    }
});

WebSocketTransport* self(void* userData) {
    return static_cast<WebSocketTransport*>(userData);
}

bool onOpenCallback(int, const EmscriptenWebSocketOpenEvent* event, void* userData) {
    self(userData)->onOpen(event->socket);
    return true;
}

bool onMessageCallback(int, const EmscriptenWebSocketMessageEvent* event, void* userData) {
    self(userData)->onMessage(event->socket, event->data, event->numBytes, event->isText);
    return true;
}

bool onErrorCallback(int, const EmscriptenWebSocketErrorEvent* event, void* userData) {
    self(userData)->onError(event->socket);
    return true;
}

bool onCloseCallback(int, const EmscriptenWebSocketCloseEvent* event, void* userData) {
    self(userData)->onClose(event->socket, event->code, event->wasClean, event->reason);
    return true;
}

void pingTimerCallback(void* userData) {
    self(userData)->onPingTimer();
}

void detach(int socket) {
    // Unhook first, so a late event never reaches a transport that moved on.
    emscripten_websocket_set_onopen_callback(socket, nullptr, nullptr);
    emscripten_websocket_set_onmessage_callback(socket, nullptr, nullptr);
    emscripten_websocket_set_onerror_callback(socket, nullptr, nullptr);
    emscripten_websocket_set_onclose_callback(socket, nullptr, nullptr);
}

} // namespace

WebSocketTransport::WebSocketTransport(InboundQueue& inbound) : inbound_(inbound) {
    ++gLiveTransports;
}

WebSocketTransport::~WebSocketTransport() {
    abandon();
    if (--gLiveTransports == 0) {
        vibe_free_websocket_event_buffer();
    }
}

bool WebSocketTransport::open(const std::string& url) {
    abandon();
    if (!emscripten_websocket_is_supported()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: this browser has no WebSocket");
        return false;
    }
    EmscriptenWebSocketCreateAttributes attributes;
    emscripten_websocket_init_create_attributes(&attributes);
    attributes.url = url.c_str();
    attributes.protocols = nullptr;
    const EMSCRIPTEN_WEBSOCKET_T socket = emscripten_websocket_new(&attributes);
    if (socket <= 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: the browser refused the WebSocket URL (%d)",
                     static_cast<int>(socket));
        return false;
    }
    socket_ = socket;
    warnedBinary_ = false;
    emscripten_websocket_set_onopen_callback(socket, this, onOpenCallback);
    emscripten_websocket_set_onmessage_callback(socket, this, onMessageCallback);
    emscripten_websocket_set_onerror_callback(socket, this, onErrorCallback);
    emscripten_websocket_set_onclose_callback(socket, this, onCloseCallback);
    return true;
}

bool WebSocketTransport::send(const std::string& text) {
    if (!open_ || socket_ <= 0) {
        return false;
    }
    const EMSCRIPTEN_RESULT result = emscripten_websocket_send_utf8_text(socket_, text.c_str());
    if (result != EMSCRIPTEN_RESULT_SUCCESS) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: WebSocket send failed (%d)", static_cast<int>(result));
        return false;
    }
    traffic_.bytesOut += text.size();
    ++traffic_.framesOut;
    return true;
}

void WebSocketTransport::close(unsigned short code, const char* reason) {
    stopPing();
    if (socket_ > 0) {
        emscripten_websocket_close(socket_, code, reason);
    }
}

void WebSocketTransport::abandon() {
    stopPing();
    if (socket_ > 0) {
        detach(socket_);
        emscripten_websocket_close(socket_, 1000, "");
        emscripten_websocket_delete(socket_);
    }
    socket_ = 0;
    open_ = false;
}

void WebSocketTransport::startPing(int periodMs) {
    stopPing();
    if (periodMs > 0) {
        pingTimer_ = emscripten_set_interval(pingTimerCallback, periodMs, this);
    }
}

void WebSocketTransport::stopPing() {
    if (pingTimer_ != 0) {
        emscripten_clear_interval(pingTimer_);
        pingTimer_ = 0;
    }
}

void WebSocketTransport::onOpen(int socket) {
    if (socket != socket_) {
        return;
    }
    open_ = true;
    inbound_.push(SocketOpened{});
}

void WebSocketTransport::onMessage(int socket, const unsigned char* data, unsigned numBytes, bool isText) {
    if (socket != socket_) {
        return;
    }
    if (!isText) {
        // The server sends binary frames only for streaming, which is off
        // (plan 17). One warning per socket.
        if (!warnedBinary_) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: dropped a binary frame (%u bytes); the session is JSON text",
                        numBytes);
            warnedBinary_ = true;
        }
        return;
    }
    // numBytes counts the terminating NUL of a text frame.
    const std::size_t size = numBytes > 0 ? numBytes - 1 : 0;
    traffic_.bytesIn += size;
    ++traffic_.framesIn;
    if (size > kMaxInboundFrameBytes) {
        malformed_.warn("a text frame", "larger than the inbound cap");
        return;
    }
    InboundMessage message;
    const DecodeResult result =
        decodeSessionFrame(inbound_.protocol(), std::string_view(reinterpret_cast<const char*>(data), size), message);
    if (result.status == DecodeStatus::Malformed) {
        malformed_.warn("a malformed frame", result.reason);
        return;
    }
    if (result.status == DecodeStatus::Ok) {
        if (inbound_.protocol().isStream(message)) {
            ++traffic_.streamIn;
        }
        inbound_.push(std::move(message));
    }
}

void WebSocketTransport::onError(int socket) {
    if (socket != socket_) {
        return;
    }
    inbound_.push(SocketError{});
}

void WebSocketTransport::onClose(int socket, int code, bool clean, const char* reason) {
    if (socket != socket_) {
        return;
    }
    stopPing();
    SocketClosed closed;
    closed.code = code;
    closed.clean = clean;
    // A close reason is at most 123 bytes by the WebSocket spec.
    closed.reason.assign(std::string_view(reason).substr(0, decltype(closed.reason)::capacity()));
    detach(socket_);
    emscripten_websocket_delete(socket_);
    socket_ = 0;
    open_ = false;
    inbound_.push(std::move(closed));
}

void WebSocketTransport::onPingTimer() {
    static const std::string ping = encodePing();
    send(ping);
}

} // namespace vibe::net


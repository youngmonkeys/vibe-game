#include "vibe/net/transport/UdpTcpSocket.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>

#include "vibe/net/transport/tcp/Sockets.h"

namespace vibe::net {

struct UdpTcpTransport::UdpConnection {
    int id = 0;
    std::string host;
    std::string port;
    std::string handshake;
    std::mutex mutex;
    sockets::Handle fd = sockets::kNone;
    std::atomic<bool> stop{false};
    std::atomic<bool> acknowledged{false};
};

namespace {

constexpr int kPollMs = 100;
constexpr std::size_t kMaxDatagramBytes = 64 * 1024;

std::unordered_map<int, UdpTcpTransport*> gUdpTransports;
int gNextUdpSocket = 1;

struct UdpEvent {
    int socket = 0;
    bool failed = false;
    std::string bytes;
    std::string reason;
};

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

void SDLCALL deliverUdp(void* userdata) {
    std::unique_ptr<UdpEvent> event(static_cast<UdpEvent*>(userdata));
    const auto it = gUdpTransports.find(event->socket);
    if (it != gUdpTransports.end()) {
        if (event->failed) {
            it->second->onUdpFailed(event->socket, event->reason.c_str());
        } else {
            it->second->onUdpDatagram(event->socket, event->bytes);
        }
    }
    wipe(event->bytes);
}

void postUdp(int socket, bool failed, std::string bytes, std::string reason = {}) {
    auto* event = new UdpEvent{socket, failed, std::move(bytes), std::move(reason)};
    if (!SDL_RunOnMainThread(deliverUdp, event, false)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: cannot hand a UDP event to the main thread: %s",
                    SDL_GetError());
        wipe(event->bytes);
        delete event;
    }
}

sockets::Handle dialUdp(const UdpTcpTransport::UdpConnection& connection, std::string& reason) {
    if (!sockets::startup(reason)) {
        return sockets::kNone;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* list = nullptr;
    const int resolved = getaddrinfo(connection.host.c_str(), connection.port.c_str(), &hints, &list);
    if (resolved != 0) {
        reason = std::string("cannot resolve the host: ") + gai_strerror(resolved);
        return sockets::kNone;
    }
    sockets::Handle connected = sockets::kNone;
    reason = "no address to connect to";
    for (addrinfo* address = list; address && connected == sockets::kNone; address = address->ai_next) {
        const sockets::Handle fd = sockets::open(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == sockets::kNone) {
            reason = sockets::errorText(sockets::lastError());
            continue;
        }
        if (connect(fd, address->ai_addr, static_cast<int>(address->ai_addrlen)) != 0) {
            reason = sockets::errorText(sockets::lastError());
            sockets::close(fd);
            continue;
        }
        connected = fd;
    }
    freeaddrinfo(list);
    return connected;
}

int SDLCALL runUdp(void* userdata) {
    std::unique_ptr<std::shared_ptr<UdpTcpTransport::UdpConnection>> holder(
        static_cast<std::shared_ptr<UdpTcpTransport::UdpConnection>*>(userdata));
    UdpTcpTransport::UdpConnection& connection = **holder;
    std::string reason;
    const sockets::Handle fd = dialUdp(connection, reason);
    if (fd == sockets::kNone) {
        wipe(connection.handshake);
        postUdp(connection.id, true, {}, reason);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(connection.mutex);
        if (connection.stop.load()) {
            sockets::close(fd);
            wipe(connection.handshake);
            return 0;
        }
        connection.fd = fd;
    }
    std::string buffer(kMaxDatagramBytes, '\0');
    int attempts = 0;
    std::uint64_t nextHandshakeAt = 0;
    while (!connection.stop.load()) {
        if (!connection.acknowledged.load()) {
            const std::uint64_t now = SDL_GetTicks();
            if (now >= nextHandshakeAt) {
                if (attempts >= UdpTcpTransport::kHandshakeAttempts) {
                    postUdp(connection.id, true, {}, "the server did not answer the UDP handshake");
                    break;
                }
                sockets::send(fd, connection.handshake.data(), connection.handshake.size());
                ++attempts;
                nextHandshakeAt = now + UdpTcpTransport::kHandshakeRetryMs;
            }
        }
        const int ready = sockets::waitReadable(fd, kPollMs);
        if (ready < 0 && !sockets::interrupted(sockets::lastError())) {
            postUdp(connection.id, true, {}, sockets::errorText(sockets::lastError()));
            break;
        }
        if (ready <= 0) {
            continue;
        }
        const sockets::IoResult received = sockets::receive(fd, buffer.data(), buffer.size());
        if (received > 0) {
            postUdp(connection.id, false, std::string(buffer.data(), static_cast<std::size_t>(received)));
        }
    }
    std::fill(buffer.begin(), buffer.end(), '\0');
    wipe(connection.handshake);
    {
        std::lock_guard<std::mutex> lock(connection.mutex);
        sockets::close(connection.fd);
        connection.fd = sockets::kNone;
    }
    return 0;
}

} // namespace

UdpTcpTransport::UdpTcpTransport(InboundQueue& inbound, const TcpOptions& options)
    : TcpTransport(inbound, options), udpReader_(kMaxDatagramBytes) {}

UdpTcpTransport::~UdpTcpTransport() {
    onConnectionGone();
}

bool UdpTcpTransport::udpConnect(int port) {
    udpClose();
    const std::string host = connectedHost();
    if (!isOpen() || host.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: UDP connect without a TCP session; ignored");
        return false;
    }
    if (port <= 0 || port > 65535) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: UDP port %d is not a port number", port);
        return false;
    }
    auto connection = std::make_shared<UdpConnection>();
    if (!writeUdpHandshake(sessionId_, sessionToken_, connection->handshake)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: the TCP handshake gave no usable session token; no UDP");
        return false;
    }
    connection->id = gNextUdpSocket++;
    connection->host = host;
    connection->port = std::to_string(port);
    auto* holder = new std::shared_ptr<UdpConnection>(connection);
    gUdpTransports[connection->id] = this;
    SDL_Thread* thread = SDL_CreateThread(runUdp, "vibe-udp", holder);
    if (!thread) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: cannot start the UDP thread: %s", SDL_GetError());
        gUdpTransports.erase(connection->id);
        wipe(connection->handshake);
        delete holder;
        return false;
    }
    SDL_DetachThread(thread);
    udp_ = std::move(connection);
    udpSocket_ = udp_->id;
    udpReady_ = false;
    SDL_Log("net: UDP handshake to %s:%d", host.c_str(), port);
    return true;
}

void UdpTcpTransport::onUdpDatagram(int socket, std::string& bytes) {
    if (socket != udpSocket_) {
        return;
    }
    traffic_.bytesIn += bytes.size();
    udpReader_.reset();
    udpReader_.append(bytes.data(), bytes.size());
    std::uint8_t header = 0;
    std::string reason;
    const TcpFrameReader::Next next = udpReader_.nextRaw(header, udpContent_, reason);
    udpReader_.reset();
    if (next != TcpFrameReader::Next::Frame) {
        malformed_.warn("a UDP datagram",
                        next == TcpFrameReader::Next::NeedMore ? "a truncated frame" : reason.c_str());
        return;
    }
    if (!openContent(header, udpContent_, "an encrypted UDP datagram")) {
        return;
    }
    int code = 0;
    if (readUdpHandshake(udpContent_, code)) {
        wipe(udpContent_);
        if (udpReady_) {
            return;
        }
        if (code == kUdpHandshakeOk) {
            udpReady_ = true;
            udp_->acknowledged.store(true);
            SDL_Log("net: UDP handshake ok; the server may send `s` over UDP");
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "net: the server refused the UDP handshake (code %d); `s` stays on TCP", code);
            udpClose();
        }
        return;
    }
    const std::uint64_t poses = traffic_.streamIn;
    pushContent(header, udpContent_, "a UDP datagram");
    ++traffic_.udpFramesIn;
    if (traffic_.streamIn != poses) {
        ++traffic_.udpStreamIn;
    }
}

void UdpTcpTransport::onUdpFailed(int socket, const char* reason) {
    if (socket != udpSocket_) {
        return;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: UDP: %s; `s` stays on TCP", reason);
    udpClose();
}

void UdpTcpTransport::onSessionAssigned(std::int64_t sessionId, const std::string& token) {
    sessionId_ = sessionId;
    sessionToken_ = token;
}

void UdpTcpTransport::onConnectionGone() {
    udpClose();
    wipe(sessionToken_);
    sessionId_ = 0;
}

void UdpTcpTransport::udpClose() {
    if (udp_) {
        gUdpTransports.erase(udpSocket_);
        udp_->stop.store(true);
    }
    udp_.reset();
    udpSocket_ = 0;
    udpReady_ = false;
    udpReader_.reset();
    wipe(udpContent_);
}

} // namespace vibe::net

#include "vibe/net/transport/TcpSocket.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_thread.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <iterator>
#include <mutex>
#include <unordered_map>
#include <variant>

#include "vibe/net/Decode.h"
#include "vibe/net/Encode.h"
#include "vibe/net/Url.h"
#include "vibe/net/transport/TcpCrypto.h"
#include "vibe/net/transport/tcp/Sockets.h"

namespace vibe::net {

struct TcpTransport::Connection {
    int id = 0;
    std::string host;
    std::string port;
    std::mutex mutex;
    sockets::Handle fd = sockets::kNone;
    std::atomic<bool> stop{false};
    TcpCrypto crypto;
};

namespace {

#ifdef VIBE_DEV
constexpr bool kDevBuild = true;
#else
constexpr bool kDevBuild = false;
#endif

constexpr bool kRequireSignedTcpHandshake = true;
constexpr bool kAllowPlainSecrets = kDevBuild;
constexpr bool kAllowUnverifiedServer = kDevBuild || !kRequireSignedTcpHandshake;

std::unordered_map<int, TcpTransport*> gTransports;
std::unordered_map<SDL_TimerID, TcpTransport*> gTimers;
int gNextSocket = 1;

enum class EventKind { Connected, Bytes, Failed, Ended };

struct Event {
    int socket = 0;
    EventKind kind = EventKind::Connected;
    std::string bytes;
    std::string reason;
    bool error = false;
};

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

void SDLCALL deliver(void* userdata) {
    std::unique_ptr<Event> event(static_cast<Event*>(userdata));
    const auto it = gTransports.find(event->socket);
    if (it != gTransports.end()) {
        TcpTransport* transport = it->second;
        switch (event->kind) {
        case EventKind::Connected: transport->onConnected(event->socket); break;
        case EventKind::Bytes: transport->onBytes(event->socket, event->bytes); break;
        case EventKind::Failed: transport->onFailed(event->socket, event->reason.c_str()); break;
        case EventKind::Ended: transport->onEnded(event->socket, event->error); break;
        }
    }
    wipe(event->bytes);
}

void post(int socket, EventKind kind, std::string bytes = {}, std::string reason = {}, bool error = false) {
    auto* event = new Event{socket, kind, std::move(bytes), std::move(reason), error};
    if (!SDL_RunOnMainThread(deliver, event, false)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: cannot hand a TCP event to the main thread: %s",
                    SDL_GetError());
        wipe(event->bytes);
        delete event;
    }
}

bool waitConnected(sockets::Handle fd, const TcpTransport::Connection& connection, std::string& reason) {
    const std::uint64_t deadline = SDL_GetTicks() + TcpTransport::kConnectTimeoutMs;
    while (!connection.stop.load()) {
        const std::uint64_t now = SDL_GetTicks();
        if (now >= deadline) {
            reason = "connect timed out";
            return false;
        }
        const int ready =
            sockets::waitWritable(fd, static_cast<int>(std::min<std::uint64_t>(100, deadline - now)));
        if (ready < 0 && !sockets::interrupted(sockets::lastError())) {
            reason = sockets::errorText(sockets::lastError());
            return false;
        }
        if (ready > 0) {
            const int error = sockets::connectResult(fd);
            if (error != 0) {
                reason = sockets::errorText(error);
                return false;
            }
            return true;
        }
    }
    reason = "abandoned";
    return false;
}

sockets::Handle dial(const TcpTransport::Connection& connection, std::string& reason) {
    if (!sockets::startup(reason)) {
        return sockets::kNone;
    }
    addrinfo hints{};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* list = nullptr;
    const int resolved = getaddrinfo(connection.host.c_str(), connection.port.c_str(), &hints, &list);
    if (resolved != 0) {
        reason = std::string("cannot resolve the host: ") + gai_strerror(resolved);
        return sockets::kNone;
    }
    sockets::Handle connected = sockets::kNone;
    reason = "no address to connect to";
    for (addrinfo* address = list; address && connected == sockets::kNone && !connection.stop.load();
         address = address->ai_next) {
        const sockets::Handle fd = sockets::open(address->ai_family, address->ai_socktype, address->ai_protocol);
        if (fd == sockets::kNone) {
            reason = sockets::errorText(sockets::lastError());
            continue;
        }
        bool ok = sockets::setBlocking(fd, false);
        if (!ok) {
            reason = sockets::errorText(sockets::lastError());
        } else if (connect(fd, address->ai_addr, static_cast<int>(address->ai_addrlen)) != 0) {
            const int error = sockets::lastError();
            if (sockets::connectPending(error)) {
                ok = waitConnected(fd, connection, reason);
            } else {
                reason = sockets::errorText(error);
                ok = false;
            }
        }
        if (ok && sockets::setBlocking(fd, true)) {
            sockets::setNoDelay(fd);
            connected = fd;
        } else {
            sockets::close(fd);
        }
    }
    freeaddrinfo(list);
    return connected;
}

int SDLCALL run(void* userdata) {
    std::unique_ptr<std::shared_ptr<TcpTransport::Connection>> holder(
        static_cast<std::shared_ptr<TcpTransport::Connection>*>(userdata));
    TcpTransport::Connection& connection = **holder;
    std::string reason;
    if (!connection.crypto.generate(reason)) {
        post(connection.id, EventKind::Failed, {}, reason);
        return 0;
    }
    const sockets::Handle fd = dial(connection, reason);
    if (fd == sockets::kNone) {
        post(connection.id, EventKind::Failed, {}, reason);
        return 0;
    }
    {
        std::lock_guard<std::mutex> lock(connection.mutex);
        if (connection.stop.load()) {
            sockets::close(fd);
            return 0;
        }
        connection.fd = fd;
    }
    post(connection.id, EventKind::Connected);
    char buffer[16 * 1024];
    bool error = false;
    for (;;) {
        const sockets::IoResult received = sockets::receive(fd, buffer, sizeof buffer);
        if (received > 0) {
            post(connection.id, EventKind::Bytes, std::string(buffer, static_cast<std::size_t>(received)));
            continue;
        }
        if (received < 0 && sockets::interrupted(sockets::lastError())) {
            continue;
        }
        error = received < 0 && !connection.stop.load();
        break;
    }
    std::fill(std::begin(buffer), std::end(buffer), '\0');
    {
        std::lock_guard<std::mutex> lock(connection.mutex);
        sockets::close(connection.fd);
        connection.fd = sockets::kNone;
    }
    post(connection.id, EventKind::Ended, {}, {}, error);
    return 0;
}

void SDLCALL pingOnMainThread(void* userdata) {
    const auto timer = static_cast<SDL_TimerID>(reinterpret_cast<std::uintptr_t>(userdata));
    const auto it = gTimers.find(timer);
    if (it != gTimers.end()) {
        it->second->onPingTimer();
    }
}

Uint32 SDLCALL pingTimerFired(void*, SDL_TimerID timer, Uint32 interval) {
    SDL_RunOnMainThread(pingOnMainThread, reinterpret_cast<void*>(static_cast<std::uintptr_t>(timer)), false);
    return interval;
}

} // namespace

TcpTransport::TcpTransport(InboundQueue& inbound) : inbound_(inbound), reader_(kMaxInboundFrameBytes) {}

TcpTransport::~TcpTransport() {
    abandon();
}

bool TcpTransport::open(const std::string& url) {
    abandon();
    ParsedUrl parsed;
    if (!parseUrl(url, parsed) || parsed.scheme != "tcp" || parsed.port == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: the socket URL is not tcp://host:port");
        return false;
    }
    auto connection = std::make_shared<Connection>();
    connection->id = gNextSocket++;
    connection->host = parsed.host;
    connection->port = std::to_string(parsed.port);
    auto* holder = new std::shared_ptr<Connection>(connection);
    gTransports[connection->id] = this;
    SDL_Thread* thread = SDL_CreateThread(run, "vibe-tcp", holder);
    if (!thread) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: cannot start the TCP thread: %s", SDL_GetError());
        gTransports.erase(connection->id);
        delete holder;
        return false;
    }
    SDL_DetachThread(thread);
    connection_ = std::move(connection);
    socket_ = connection_->id;
    open_ = false;
    closing_ = false;
    awaitingHandshake_ = false;
    reader_.reset();
    return true;
}

bool TcpTransport::send(const std::string& text) {
    if (!open_ || !connection_) {
        return false;
    }
    std::string error;
    int command = 0;
    bool secret = false;
    TcpCrypto& crypto = connection_->crypto;
    if (!encodeTcpContent(text, crypto.publicKey(), outContent_, command, secret, error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: cannot encode a TCP frame: %s", error.c_str());
        return false;
    }
    std::uint8_t flags = 0;
    if (command == kTcpCommandHandshake) {
        awaitingHandshake_ = true;
    } else if (secret) {
        if (crypto.hasSessionKey()) {
            if (!crypto.encrypt(outContent_, sealed_, error)) {
                wipe(outContent_);
                SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: cannot encrypt a TCP frame (command %d): %s", command,
                             error.c_str());
                return false;
            }
            outContent_.swap(sealed_);
            wipe(sealed_);
            flags = kTcpHeaderEncrypted;
        } else if (kAllowPlainSecrets) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "net: the server gave no session key; command %d goes in clear (dev build only)", command);
        } else {
            wipe(outContent_);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION,
                         "net: the server gave no session key; command %d is not sent", command);
            return false;
        }
    }
    writeTcpFrame(flags, outContent_, outFrame_);
    wipe(outContent_);
    bool sent = true;
    int sendError = 0;
    {
        std::lock_guard<std::mutex> lock(connection_->mutex);
        const sockets::Handle fd = connection_->fd;
        std::size_t offset = 0;
        while (fd != sockets::kNone && offset < outFrame_.size()) {
            const sockets::IoResult written = sockets::send(fd, outFrame_.data() + offset, outFrame_.size() - offset);
            if (written > 0) {
                offset += static_cast<std::size_t>(written);
                continue;
            }
            sendError = sockets::lastError();
            if (written < 0 && sockets::interrupted(sendError)) {
                continue;
            }
            break;
        }
        sent = fd != sockets::kNone && offset == outFrame_.size();
    }
    const std::size_t size = outFrame_.size();
    wipe(outFrame_);
    if (!sent) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: TCP send failed: %s", sockets::errorText(sendError).c_str());
        return false;
    }
    traffic_.bytesOut += size;
    ++traffic_.framesOut;
    return true;
}

void TcpTransport::close(unsigned short code, const char*) {
    stopPing();
    if (!connection_) {
        return;
    }
    closing_ = true;
    closeCode_ = code;
    std::lock_guard<std::mutex> lock(connection_->mutex);
    sockets::shutdownBoth(connection_->fd);
}

void TcpTransport::abandon() {
    stopPing();
    onConnectionGone();
    if (connection_) {
        gTransports.erase(socket_);
        connection_->stop.store(true);
        std::lock_guard<std::mutex> lock(connection_->mutex);
        sockets::shutdownBoth(connection_->fd);
    }
    connection_.reset();
    socket_ = 0;
    open_ = false;
    closing_ = false;
    awaitingHandshake_ = false;
    reader_.reset();
}

void TcpTransport::startPing(int periodMs) {
    stopPing();
    if (periodMs > 0) {
        pingTimer_ = SDL_AddTimer(static_cast<Uint32>(periodMs), pingTimerFired, nullptr);
        if (pingTimer_ != 0) {
            gTimers[pingTimer_] = this;
        }
    }
}

void TcpTransport::stopPing() {
    if (pingTimer_ != 0) {
        SDL_RemoveTimer(pingTimer_);
        gTimers.erase(pingTimer_);
        pingTimer_ = 0;
    }
}

void TcpTransport::onConnected(int socket) {
    if (socket != socket_) {
        return;
    }
    open_ = true;
    inbound_.push(SocketOpened{});
}

void TcpTransport::onBytes(int socket, std::string& bytes) {
    if (socket != socket_) {
        return;
    }
    traffic_.bytesIn += bytes.size();
    reader_.append(bytes.data(), bytes.size());
    std::string reason;
    std::uint8_t header = 0;
    for (;;) {
        const TcpFrameReader::Next next = reader_.nextRaw(header, inContent_, reason);
        if (next == TcpFrameReader::Next::NeedMore) {
            break;
        }
        if (next == TcpFrameReader::Next::Skipped) {
            malformed_.warn("a TCP frame", reason.c_str());
            continue;
        }
        if (!openContent(header, inContent_, "an encrypted TCP frame")) {
            continue;
        }
        if (awaitingHandshake_ && !(header & kTcpHeaderEncrypted) && !readHandshake(inContent_)) {
            wipe(inContent_);
            return;
        }
        pushContent(header, inContent_, "a TCP frame");
    }
}

std::string TcpTransport::connectedHost() const {
    return connection_ ? connection_->host : std::string();
}

bool TcpTransport::openContent(std::uint8_t& header, std::string& content, const char* what) {
    if (!(header & kTcpHeaderEncrypted) || !connection_ || !connection_->crypto.hasSessionKey()) {
        return true;
    }
    std::string error;
    const bool opened = connection_->crypto.decrypt(content, opened_, error);
    wipe(content);
    if (!opened) {
        malformed_.warn(what, error.c_str());
        return false;
    }
    content.swap(opened_);
    header = static_cast<std::uint8_t>(header & ~kTcpHeaderEncrypted);
    return true;
}

void TcpTransport::pushContent(std::uint8_t header, std::string& content, const char* what) {
    std::string reason;
    const bool decoded = decodeTcpContent(header, content, inFrame_, reason);
    wipe(content);
    if (!decoded) {
        malformed_.warn(what, reason.c_str());
        return;
    }
    ++traffic_.framesIn;
    InboundMessage message;
    const DecodeResult result = decodeSessionFrame(inbound_.protocol(), inFrame_, message);
    wipe(inFrame_);
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

void TcpTransport::onFailed(int socket, const char* reason) {
    if (socket != socket_) {
        return;
    }
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: TCP connect failed: %s", reason);
    inbound_.push(SocketError{});
    finish(1006, false, reason);
}

void TcpTransport::onEnded(int socket, bool error) {
    if (socket != socket_) {
        return;
    }
    if (error && !closing_) {
        inbound_.push(SocketError{});
    }
    if (closing_) {
        finish(closeCode_, true, "");
    } else {
        finish(1006, false, error ? "connection error" : "the server closed the connection");
    }
}

void TcpTransport::onPingTimer() {
    static const std::string ping = encodePing();
    send(ping);
}

bool TcpTransport::readHandshake(const std::string& content) {
    std::string key;
    std::string proof;
    if (!readTcpHandshake(content, key, proof)) {
        return true;
    }
    awaitingHandshake_ = false;
    std::int64_t sessionId = 0;
    std::string token;
    if (readTcpHandshakeSession(content, sessionId, token)) {
        onSessionAssigned(sessionId, token);
    }
    wipe(token);
    if (key.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "net: the server did not turn on TCP encryption (socket ssl-active)");
        return true;
    }
    if (!connection_) {
        wipe(key);
        return false;
    }
    std::string error;
    if (proof.empty()) {
        if (!kAllowUnverifiedServer) {
            wipe(key);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: the server did not sign the TCP handshake");
            drop("the TCP server is not authenticated");
            return false;
        }
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                    "net: the server did not sign the TCP handshake; it is not authenticated (not enforced)");
    } else {
        bool trusted = false;
        std::string subject;
        if (!connection_->crypto.verifyServer(proof, key, connection_->host, trusted, subject, error)) {
            wipe(key);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: TCP handshake: %s", error.c_str());
            drop("the TCP handshake signature is not valid");
            return false;
        }
        if (trusted) {
            SDL_Log("net: TCP server verified as %s (%s)", connection_->host.c_str(), subject.c_str());
        } else if (kAllowUnverifiedServer) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                        "net: TCP handshake signed by %s, which is not trusted for %s (not enforced)",
                        subject.c_str(), connection_->host.c_str());
        } else {
            wipe(key);
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: the TCP server certificate %s is not trusted for %s",
                         subject.c_str(), connection_->host.c_str());
            drop("the TCP server certificate is not trusted");
            return false;
        }
    }
    const bool accepted = connection_->crypto.acceptSessionKey(key, error);
    wipe(key);
    if (!accepted) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "net: TCP handshake: %s", error.c_str());
        drop("the TCP session key is not usable");
        return false;
    }
    SDL_Log("net: TCP session key accepted; the login and chat are encrypted");
    return true;
}

void TcpTransport::drop(const char* reason) {
    if (connection_) {
        connection_->stop.store(true);
        std::lock_guard<std::mutex> lock(connection_->mutex);
        sockets::shutdownBoth(connection_->fd);
    }
    inbound_.push(SocketError{});
    finish(1006, false, reason);
}

void TcpTransport::finish(int code, bool clean, const char* reason) {
    stopPing();
    onConnectionGone();
    gTransports.erase(socket_);
    connection_.reset();
    socket_ = 0;
    open_ = false;
    closing_ = false;
    awaitingHandshake_ = false;
    reader_.reset();
    SocketClosed closed;
    closed.code = code;
    closed.clean = clean;
    closed.reason.assign(std::string_view(reason).substr(0, decltype(closed.reason)::capacity()));
    inbound_.push(std::move(closed));
}

} // namespace vibe::net

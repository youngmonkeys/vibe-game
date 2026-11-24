#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vibe/net/Queues.h"
#include "vibe/net/TcpCodec.h"
#include "vibe/net/WarnLimiter.h"
#include "vibe/net/transport/Traffic.h"

namespace vibe::net {

class TcpTransport {
public:
    static constexpr int kConnectTimeoutMs = 10000;

    explicit TcpTransport(InboundQueue& inbound);
    virtual ~TcpTransport();
    TcpTransport(const TcpTransport&) = delete;
    TcpTransport& operator=(const TcpTransport&) = delete;

    bool open(const std::string& url);
    bool send(const std::string& text);
    void close(unsigned short code, const char* reason);
    void abandon();
    void startPing(int periodMs);
    void stopPing();

    virtual bool usesUdp() const { return false; }
    virtual bool udpConnect(int /*port*/) { return false; }

    bool isOpen() const { return open_; }
    const TrafficCounters& traffic() const { return traffic_; }

    struct Connection;

    void onConnected(int socket);
    void onBytes(int socket, std::string& bytes);
    void onFailed(int socket, const char* reason);
    void onEnded(int socket, bool error);
    void onPingTimer();

protected:
    virtual void onSessionAssigned(std::int64_t /*sessionId*/, const std::string& /*token*/) {}
    virtual void onConnectionGone() {}

    std::string connectedHost() const;
    bool openContent(std::uint8_t& header, std::string& content, const char* what);
    void pushContent(std::uint8_t header, std::string& content, const char* what);

    TrafficCounters traffic_;
    WarnLimiter malformed_;

private:
    void finish(int code, bool clean, const char* reason);
    void drop(const char* reason);
    bool readHandshake(const std::string& content);

    InboundQueue& inbound_;
    std::shared_ptr<Connection> connection_;
    int socket_ = 0;
    bool open_ = false;
    bool closing_ = false;
    bool awaitingHandshake_ = false;
    int closeCode_ = 1000;
    unsigned pingTimer_ = 0;
    TcpFrameReader reader_;
    std::string outFrame_;
    std::string outContent_;
    std::string sealed_;
    std::string inFrame_;
    std::string inContent_;
    std::string opened_;
};

} // namespace vibe::net

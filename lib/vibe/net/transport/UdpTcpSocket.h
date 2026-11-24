#pragma once

#include <cstdint>
#include <memory>
#include <string>

#include "vibe/net/transport/TcpSocket.h"

namespace vibe::net {

class UdpTcpTransport : public TcpTransport {
public:
    static constexpr int kHandshakeAttempts = 5;
    static constexpr int kHandshakeRetryMs = 1000;

    explicit UdpTcpTransport(InboundQueue& inbound);
    ~UdpTcpTransport() override;

    bool usesUdp() const override { return true; }
    bool udpConnect(int port) override;
    bool udpReady() const { return udpReady_; }

    struct UdpConnection;

    void onUdpDatagram(int socket, std::string& bytes);
    void onUdpFailed(int socket, const char* reason);

protected:
    void onSessionAssigned(std::int64_t sessionId, const std::string& token) override;
    void onConnectionGone() override;

private:
    void udpClose();

    std::shared_ptr<UdpConnection> udp_;
    int udpSocket_ = 0;
    bool udpReady_ = false;
    std::int64_t sessionId_ = 0;
    std::string sessionToken_;
    TcpFrameReader udpReader_;
    std::string udpContent_;
};

} // namespace vibe::net

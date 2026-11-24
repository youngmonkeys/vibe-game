#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace vibe::net {

inline constexpr std::uint8_t kTcpHeaderBigSize = 0x01;
inline constexpr std::uint8_t kTcpHeaderEncrypted = 0x02;
inline constexpr std::uint8_t kTcpHeaderCompressed = 0x04;
inline constexpr std::uint8_t kTcpHeaderText = 0x08;
inline constexpr std::uint8_t kTcpHeaderRawBytes = 0x10;
inline constexpr std::uint8_t kTcpHeaderUdpHandshake = 0x20;

inline constexpr std::size_t kMaxSmallTcpContentBytes = 0xFFFF;
inline constexpr int kTcpCommandHandshake = 11;
inline constexpr int kTcpCommandLogin = 20;
inline constexpr int kTcpCommandAppRequest = 31;
inline constexpr int kTcpCommandUdpHandshake = 50;
inline constexpr int kUdpHandshakeOk = 200;
inline constexpr std::size_t kMaxUdpHandshakeTokenBytes = 512;
inline constexpr int kMsgPackMaxDepth = 64;
inline constexpr std::uint8_t kTcpServerProofVersion = 1;
inline constexpr std::string_view kTcpHandshakeSignatureDomain = "ezyfox-handshake-v1";

struct TcpServerProof {
    std::string signature;
    std::vector<std::string> certificates;
};

bool msgpackToJsonText(std::string_view bytes, std::string& jsonText, std::string& error);

bool jsonTextToMsgpack(std::string_view jsonText, std::string& out);

bool encodeTcpFrame(std::string_view jsonFrame, std::string& out, std::string& error);

bool encodeTcpContent(std::string_view jsonFrame, std::string_view clientKey, std::string& content, int& command,
                      bool& secret, std::string& error);

void writeTcpFrame(std::uint8_t flags, std::string_view content, std::string& out);

bool decodeTcpContent(std::uint8_t header, std::string_view content, std::string& jsonFrame, std::string& reason);

bool readTcpHandshake(std::string_view content, std::string& key, std::string& proof);

bool readTcpHandshakeSession(std::string_view content, std::int64_t& sessionId, std::string& token);

bool writeUdpHandshake(std::int64_t sessionId, std::string_view token, std::string& out);

bool readUdpHandshake(std::string_view content, int& code);

bool parseTcpServerProof(std::string_view bytes, TcpServerProof& out);

std::string tcpHandshakeSignedMessage(std::string_view clientKey, std::string_view encryptedKey);

class TcpFrameReader {
public:
    enum class Next { NeedMore, Frame, Skipped };

    explicit TcpFrameReader(std::size_t maxContentBytes) : maxContentBytes_(maxContentBytes) {}

    void append(const char* data, std::size_t size);
    Next next(std::string& jsonFrame, std::string& reason);
    Next nextRaw(std::uint8_t& header, std::string& content, std::string& reason);
    void reset();
    std::size_t buffered() const { return buffer_.size() - offset_; }

private:
    void compact();

    std::size_t maxContentBytes_;
    std::string buffer_;
    std::size_t offset_ = 0;
    std::size_t skipRemaining_ = 0;
};

} // namespace vibe::net

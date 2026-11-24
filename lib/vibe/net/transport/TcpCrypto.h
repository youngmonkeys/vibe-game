#pragma once

#include <string>
#include <string_view>

namespace vibe::net {

class TcpCrypto {
public:
    TcpCrypto() = default;
    ~TcpCrypto();
    TcpCrypto(const TcpCrypto&) = delete;
    TcpCrypto& operator=(const TcpCrypto&) = delete;

    bool generate(std::string& error);
    const std::string& publicKey() const { return publicKey_; }
    bool verifyServer(std::string_view proof, std::string_view encryptedKey, const std::string& host, bool& trusted,
                      std::string& subject, std::string& error) const;
    bool acceptSessionKey(std::string_view encryptedKey, std::string& error);
    bool hasSessionKey() const { return !sessionKey_.empty(); }
    bool encrypt(std::string_view plain, std::string& out, std::string& error) const;
    bool decrypt(std::string_view data, std::string& out, std::string& error) const;
    void clear();

private:
    void releasePrivateKey();

    void* privateKey_ = nullptr;
    std::string publicKey_;
    std::string sessionKey_;
};

} // namespace vibe::net

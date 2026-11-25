#include <cstdint>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

#include "vibe/net/TcpCodec.h"

#if defined(VIBE_TEST_APPLE_SECURITY)
#include <Security/Security.h>
#elif defined(VIBE_TEST_OPENSSL)
#include <openssl/evp.h>
#include <openssl/x509.h>
#endif

namespace {

int failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::printf("FAIL line %d: %s\n", line, what);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __LINE__)

constexpr const char* kProofFile = "ssl-handshake-proof.hex";

std::string byteRange(int from, int to) {
    std::string out;
    for (int i = from; i < to; ++i) {
        out.push_back(static_cast<char>(i));
    }
    return out;
}

std::string clientKey() {
    return byteRange(0x01, 0x21);
}

std::string encryptedKey() {
    return byteRange(0xA0, 0xC0);
}

std::string field(std::string_view value) {
    std::string out;
    out.push_back(static_cast<char>((value.size() >> 8) & 0xFF));
    out.push_back(static_cast<char>(value.size() & 0xFF));
    out.append(value);
    return out;
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

bool readHexFile(const char* path, std::string& out) {
    out.clear();
    std::ifstream in(path);
    if (!in) {
        return false;
    }
    std::stringstream buffer;
    buffer << in.rdbuf();
    int high = -1;
    for (char c : buffer.str()) {
        const int value = hexValue(c);
        if (value < 0) {
            continue;
        }
        if (high < 0) {
            high = value;
        } else {
            out.push_back(static_cast<char>((high << 4) | value));
            high = -1;
        }
    }
    return high < 0 && !out.empty();
}

#if defined(VIBE_TEST_APPLE_SECURITY)

CFDataRef dataOf(std::string_view bytes) {
    return CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.data()),
                        static_cast<CFIndex>(bytes.size()));
}

bool verifySignature(const std::string& certificateDer, const std::string& message, const std::string& signature) {
    CFDataRef certificateData = dataOf(certificateDer);
    SecCertificateRef certificate = SecCertificateCreateWithData(kCFAllocatorDefault, certificateData);
    CFRelease(certificateData);
    if (!certificate) {
        return false;
    }
    SecKeyRef key = SecCertificateCopyKey(certificate);
    CFRelease(certificate);
    if (!key) {
        return false;
    }
    CFDataRef messageData = dataOf(message);
    CFDataRef signatureData = dataOf(signature);
    const bool verified = SecKeyVerifySignature(key, kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256, messageData,
                                                signatureData, nullptr);
    CFRelease(signatureData);
    CFRelease(messageData);
    CFRelease(key);
    return verified;
}

#elif defined(VIBE_TEST_OPENSSL)

bool verifySignature(const std::string& certificateDer, const std::string& message, const std::string& signature) {
    const unsigned char* cursor = reinterpret_cast<const unsigned char*>(certificateDer.data());
    X509* certificate = d2i_X509(nullptr, &cursor, static_cast<long>(certificateDer.size()));
    if (!certificate) {
        return false;
    }
    EVP_PKEY* key = X509_get_pubkey(certificate);
    X509_free(certificate);
    if (!key) {
        return false;
    }
    EVP_MD_CTX* context = EVP_MD_CTX_new();
    const bool verified =
        context && EVP_DigestVerifyInit(context, nullptr, EVP_sha256(), nullptr, key) == 1 &&
        EVP_DigestVerify(context, reinterpret_cast<const unsigned char*>(signature.data()), signature.size(),
                         reinterpret_cast<const unsigned char*>(message.data()), message.size()) == 1;
    EVP_MD_CTX_free(context);
    EVP_PKEY_free(key);
    return verified;
}

#endif

void testSignedMessage() {
    const std::string small = std::string(vibe::net::kTcpHandshakeSignatureDomain) + std::string("\0\2ck\0\1e", 7);
    CHECK(vibe::net::tcpHandshakeSignedMessage("ck", "e") == small);

    const std::string expected =
        std::string(vibe::net::kTcpHandshakeSignatureDomain) + field(clientKey()) + field(encryptedKey());
    CHECK(vibe::net::tcpHandshakeSignedMessage(clientKey(), encryptedKey()) == expected);
}

void testParseProof(const std::string& proof) {
    vibe::net::TcpServerProof parsed;
    CHECK(vibe::net::parseTcpServerProof(proof, parsed));
    CHECK(parsed.signature.size() == 256);
    CHECK(parsed.certificates.size() == 1);
    if (parsed.certificates.size() != 1) {
        return;
    }
    const std::string& certificate = parsed.certificates.front();
    CHECK(certificate.size() > 2 && static_cast<std::uint8_t>(certificate[0]) == 0x30);
    const std::string rebuilt = std::string(1, static_cast<char>(vibe::net::kTcpServerProofVersion)) +
                                field(parsed.signature) + std::string(1, '\1') + field(certificate);
    CHECK(rebuilt == proof);
}

void testMalformedProofs(const std::string& proof) {
    vibe::net::TcpServerProof parsed;
    CHECK(!vibe::net::parseTcpServerProof("", parsed));

    std::string wrongVersion = proof;
    wrongVersion[0] = 2;
    CHECK(!vibe::net::parseTcpServerProof(wrongVersion, parsed));

    CHECK(!vibe::net::parseTcpServerProof(proof + "x", parsed));
    CHECK(!vibe::net::parseTcpServerProof(proof.substr(0, proof.size() - 1), parsed));

    vibe::net::TcpServerProof valid;
    if (!vibe::net::parseTcpServerProof(proof, valid) || valid.certificates.empty()) {
        CHECK(false);
        return;
    }
    const std::string head = std::string(1, '\1') + field(valid.signature);
    CHECK(!vibe::net::parseTcpServerProof(head + std::string(1, '\0'), parsed));

    std::string tooMany = head + std::string(1, '\x09');
    for (int i = 0; i < 9; ++i) {
        tooMany += field(valid.certificates.front());
    }
    CHECK(!vibe::net::parseTcpServerProof(tooMany, parsed));
}

void testSignature(const std::string& proof) {
#if defined(VIBE_TEST_APPLE_SECURITY) || defined(VIBE_TEST_OPENSSL)
    vibe::net::TcpServerProof parsed;
    if (!vibe::net::parseTcpServerProof(proof, parsed) || parsed.certificates.empty()) {
        CHECK(false);
        return;
    }
    const std::string& certificate = parsed.certificates.front();
    const std::string message = vibe::net::tcpHandshakeSignedMessage(clientKey(), encryptedKey());
    CHECK(verifySignature(certificate, message, parsed.signature));

    std::string otherEncryptedKey = encryptedKey();
    otherEncryptedKey.back() = static_cast<char>(otherEncryptedKey.back() ^ 0x01);
    CHECK(!verifySignature(certificate, vibe::net::tcpHandshakeSignedMessage(clientKey(), otherEncryptedKey),
                           parsed.signature));

    std::string otherClientKey = clientKey();
    otherClientKey.front() = static_cast<char>(otherClientKey.front() ^ 0x01);
    CHECK(!verifySignature(certificate, vibe::net::tcpHandshakeSignedMessage(otherClientKey, encryptedKey()),
                           parsed.signature));

    std::string otherSignature = parsed.signature;
    otherSignature[10] = static_cast<char>(otherSignature[10] ^ 0x01);
    CHECK(!verifySignature(certificate, message, otherSignature));
#else
    (void)proof;
    std::printf("tcp-proof-tests: no platform crypto, signature checks skipped\n");
#endif
}

} // namespace

int main() {
    std::string proof;
    if (!readHexFile(kProofFile, proof)) {
        std::printf("FAIL: cannot read %s\n", kProofFile);
        return 1;
    }
    testSignedMessage();
    testParseProof(proof);
    testMalformedProofs(proof);
    testSignature(proof);
    if (failures == 0) {
        std::printf("tcp-proof-tests: all passed\n");
    }
    return failures == 0 ? 0 : 1;
}

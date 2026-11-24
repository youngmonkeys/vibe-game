#include "vibe/net/transport/TcpCrypto.h"

#include <CommonCrypto/CommonCryptor.h>
#include <Security/Security.h>

#include <algorithm>
#include <cstdint>

#include "vibe/net/TcpCodec.h"

namespace vibe::net {
namespace {

constexpr int kRsaBits = 2048;
constexpr std::size_t kSessionKeyBytes = 32;
constexpr std::size_t kIvBytes = kCCBlockSizeAES128;

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

std::string errorText(CFErrorRef error) {
    if (!error) {
        return "unknown error";
    }
    std::string text = "error " + std::to_string(static_cast<long>(CFErrorGetCode(error)));
    CFRelease(error);
    return text;
}

void appendDerLength(std::string& out, std::size_t length) {
    if (length < 0x80) {
        out.push_back(static_cast<char>(length));
        return;
    }
    std::string bytes;
    while (length > 0) {
        bytes.insert(bytes.begin(), static_cast<char>(length & 0xff));
        length >>= 8;
    }
    out.push_back(static_cast<char>(0x80 | bytes.size()));
    out += bytes;
}

void appendDer(std::string& out, unsigned char tag, std::string_view body) {
    out.push_back(static_cast<char>(tag));
    appendDerLength(out, body.size());
    out.append(body);
}

std::string subjectPublicKeyInfo(std::string_view rsaPublicKey) {
    static constexpr unsigned char kRsaAlgorithm[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                                      0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    std::string bitString(1, '\0');
    bitString.append(rsaPublicKey);
    std::string body(reinterpret_cast<const char*>(kRsaAlgorithm), sizeof kRsaAlgorithm);
    appendDer(body, 0x03, bitString);
    std::string out;
    appendDer(out, 0x30, body);
    return out;
}

std::string_view bytesOf(CFDataRef data) {
    return {reinterpret_cast<const char*>(CFDataGetBytePtr(data)), static_cast<std::size_t>(CFDataGetLength(data))};
}

template <typename T>
struct CfHolder {
    T ref = nullptr;
    explicit CfHolder(T value) : ref(value) {}
    ~CfHolder() {
        if (ref) {
            CFRelease(ref);
        }
    }
    CfHolder(const CfHolder&) = delete;
    CfHolder& operator=(const CfHolder&) = delete;
};

CFDataRef dataOf(std::string_view bytes) {
    return CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(bytes.data()),
                        static_cast<CFIndex>(bytes.size()));
}

std::string stringOf(CFStringRef text) {
    if (!text) {
        return {};
    }
    char buffer[256];
    if (!CFStringGetCString(text, buffer, sizeof buffer, kCFStringEncodingUTF8)) {
        return {};
    }
    return buffer;
}

SecKeyAlgorithm signatureAlgorithm(SecKeyRef key) {
    CfHolder<CFDictionaryRef> attributes(SecKeyCopyAttributes(key));
    if (!attributes.ref) {
        return nullptr;
    }
    const void* type = CFDictionaryGetValue(attributes.ref, kSecAttrKeyType);
    if (type && CFEqual(type, kSecAttrKeyTypeRSA)) {
        return kSecKeyAlgorithmRSASignatureMessagePKCS1v15SHA256;
    }
    if (type && CFEqual(type, kSecAttrKeyTypeECSECPrimeRandom)) {
        return kSecKeyAlgorithmECDSASignatureMessageX962SHA256;
    }
    return nullptr;
}

} // namespace

bool TcpCrypto::verifyServer(std::string_view proof, std::string_view encryptedKey, const std::string& host,
                             bool& trusted, std::string& subject, std::string& error) const {
    trusted = false;
    subject.clear();
    TcpServerProof parsed;
    if (!parseTcpServerProof(proof, parsed)) {
        error = "the server proof is malformed";
        return false;
    }
    CfHolder<CFMutableArrayRef> chain(CFArrayCreateMutable(kCFAllocatorDefault, 0, &kCFTypeArrayCallBacks));
    for (const std::string& der : parsed.certificates) {
        CfHolder<CFDataRef> data(dataOf(der));
        CfHolder<SecCertificateRef> certificate(SecCertificateCreateWithData(kCFAllocatorDefault, data.ref));
        if (!certificate.ref) {
            error = "the server certificate does not parse";
            return false;
        }
        CFArrayAppendValue(chain.ref, certificate.ref);
    }
    SecCertificateRef leaf = static_cast<SecCertificateRef>(const_cast<void*>(CFArrayGetValueAtIndex(chain.ref, 0)));
    CfHolder<CFStringRef> summary(SecCertificateCopySubjectSummary(leaf));
    subject = stringOf(summary.ref);
    CfHolder<SecKeyRef> key(SecCertificateCopyKey(leaf));
    if (!key.ref) {
        error = "the server certificate has no usable key";
        return false;
    }
    SecKeyAlgorithm algorithm = signatureAlgorithm(key.ref);
    if (!algorithm || !SecKeyIsAlgorithmSupported(key.ref, kSecKeyOperationTypeVerify, algorithm)) {
        error = "the server key type is not RSA or EC";
        return false;
    }
    CfHolder<CFDataRef> message(dataOf(tcpHandshakeSignedMessage(publicKey_, encryptedKey)));
    CfHolder<CFDataRef> signature(dataOf(parsed.signature));
    CFErrorRef cfError = nullptr;
    if (!SecKeyVerifySignature(key.ref, algorithm, message.ref, signature.ref, &cfError)) {
        error = "the handshake signature does not match: " + errorText(cfError);
        return false;
    }
    CfHolder<CFStringRef> hostName(CFStringCreateWithCString(kCFAllocatorDefault, host.c_str(), kCFStringEncodingUTF8));
    CfHolder<SecPolicyRef> policy(SecPolicyCreateSSL(true, hostName.ref));
    SecTrustRef trustRef = nullptr;
    if (SecTrustCreateWithCertificates(chain.ref, policy.ref, &trustRef) != errSecSuccess || !trustRef) {
        error = "cannot check the server certificate";
        return false;
    }
    CfHolder<SecTrustRef> trust(trustRef);
    CFErrorRef trustError = nullptr;
    trusted = SecTrustEvaluateWithError(trust.ref, &trustError);
    if (trustError) {
        CFRelease(trustError);
    }
    return true;
}

TcpCrypto::~TcpCrypto() {
    clear();
}

void TcpCrypto::releasePrivateKey() {
    if (privateKey_) {
        CFRelease(static_cast<SecKeyRef>(privateKey_));
        privateKey_ = nullptr;
    }
}

void TcpCrypto::clear() {
    releasePrivateKey();
    publicKey_.clear();
    wipe(sessionKey_);
}

bool TcpCrypto::generate(std::string& error) {
    clear();
    int bits = kRsaBits;
    CFNumberRef size = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &bits);
    const void* keys[] = {kSecAttrKeyType, kSecAttrKeySizeInBits};
    const void* values[] = {kSecAttrKeyTypeRSA, size};
    CFDictionaryRef attributes = CFDictionaryCreate(kCFAllocatorDefault, keys, values, 2,
                                                    &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFRelease(size);
    CFErrorRef cfError = nullptr;
    SecKeyRef privateKey = SecKeyCreateRandomKey(attributes, &cfError);
    CFRelease(attributes);
    if (!privateKey) {
        error = "cannot create the RSA key: " + errorText(cfError);
        return false;
    }
    SecKeyRef publicKey = SecKeyCopyPublicKey(privateKey);
    if (!publicKey) {
        CFRelease(privateKey);
        error = "the RSA key has no public key";
        return false;
    }
    CFDataRef data = SecKeyCopyExternalRepresentation(publicKey, &cfError);
    CFRelease(publicKey);
    if (!data) {
        CFRelease(privateKey);
        error = "cannot export the RSA public key: " + errorText(cfError);
        return false;
    }
    publicKey_ = subjectPublicKeyInfo(bytesOf(data));
    CFRelease(data);
    privateKey_ = privateKey;
    return true;
}

bool TcpCrypto::acceptSessionKey(std::string_view encryptedKey, std::string& error) {
    wipe(sessionKey_);
    if (!privateKey_) {
        error = "no RSA key";
        return false;
    }
    CFDataRef cipher = CFDataCreate(kCFAllocatorDefault, reinterpret_cast<const UInt8*>(encryptedKey.data()),
                                    static_cast<CFIndex>(encryptedKey.size()));
    CFErrorRef cfError = nullptr;
    CFDataRef plain = SecKeyCreateDecryptedData(static_cast<SecKeyRef>(privateKey_),
                                                kSecKeyAlgorithmRSAEncryptionPKCS1, cipher, &cfError);
    CFRelease(cipher);
    releasePrivateKey();
    if (!plain) {
        error = "cannot decrypt the session key: " + errorText(cfError);
        return false;
    }
    const std::string_view key = bytesOf(plain);
    if (key.size() == kSessionKeyBytes) {
        sessionKey_.assign(key);
    }
    CFRelease(plain);
    if (sessionKey_.empty()) {
        error = "the session key is not 32 bytes";
        return false;
    }
    return true;
}

bool TcpCrypto::encrypt(std::string_view plain, std::string& out, std::string& error) const {
    if (sessionKey_.empty()) {
        error = "no session key";
        return false;
    }
    out.assign(kIvBytes + plain.size() + kCCBlockSizeAES128, '\0');
    if (SecRandomCopyBytes(kSecRandomDefault, kIvBytes, out.data()) != errSecSuccess) {
        wipe(out);
        error = "cannot make a random IV";
        return false;
    }
    std::size_t moved = 0;
    const CCCryptorStatus status =
        CCCrypt(kCCEncrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, sessionKey_.data(), sessionKey_.size(),
                out.data(), plain.data(), plain.size(), out.data() + kIvBytes, out.size() - kIvBytes, &moved);
    if (status != kCCSuccess) {
        wipe(out);
        error = "AES encrypt failed: " + std::to_string(status);
        return false;
    }
    out.resize(kIvBytes + moved);
    return true;
}

bool TcpCrypto::decrypt(std::string_view data, std::string& out, std::string& error) const {
    if (sessionKey_.empty()) {
        error = "no session key";
        return false;
    }
    if (data.size() <= kIvBytes || (data.size() - kIvBytes) % kCCBlockSizeAES128 != 0) {
        error = "the encrypted content has a bad size";
        return false;
    }
    out.assign(data.size() - kIvBytes, '\0');
    std::size_t moved = 0;
    const CCCryptorStatus status =
        CCCrypt(kCCDecrypt, kCCAlgorithmAES, kCCOptionPKCS7Padding, sessionKey_.data(), sessionKey_.size(),
                data.data(), data.data() + kIvBytes, data.size() - kIvBytes, out.data(), out.size(), &moved);
    if (status != kCCSuccess) {
        wipe(out);
        error = "AES decrypt failed: " + std::to_string(status);
        return false;
    }
    out.resize(moved);
    return true;
}

} // namespace vibe::net

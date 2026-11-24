#include "vibe/net/transport/TcpCrypto.h"

#include <windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

#include "vibe/net/TcpCodec.h"
#include "vibe/net/transport/windows/Text.h"

namespace vibe::net {
namespace {

constexpr ULONG kRsaBits = 2048;
constexpr std::size_t kSessionKeyBytes = 32;
constexpr std::size_t kIvBytes = 16;
constexpr std::size_t kSha256Bytes = 32;

bool ok(NTSTATUS status) {
    return status >= 0;
}

std::string statusText(NTSTATUS status) {
    char text[32];
    std::snprintf(text, sizeof text, "status 0x%08lx", static_cast<unsigned long>(status));
    return text;
}

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

PUCHAR bytes(std::string_view data) {
    return reinterpret_cast<PUCHAR>(const_cast<char*>(data.data()));
}

struct Providers {
    BCRYPT_ALG_HANDLE rsa = nullptr;
    BCRYPT_ALG_HANDLE aes = nullptr;
    BCRYPT_ALG_HANDLE sha256 = nullptr;
};

const Providers& providers() {
    static const Providers shared = [] {
        Providers opened;
        if (!ok(BCryptOpenAlgorithmProvider(&opened.rsa, BCRYPT_RSA_ALGORITHM, nullptr, 0))) {
            opened.rsa = nullptr;
        }
        if (ok(BCryptOpenAlgorithmProvider(&opened.aes, BCRYPT_AES_ALGORITHM, nullptr, 0))) {
            const std::wstring mode = BCRYPT_CHAIN_MODE_CBC;
            if (!ok(BCryptSetProperty(opened.aes, BCRYPT_CHAINING_MODE,
                                      reinterpret_cast<PUCHAR>(const_cast<wchar_t*>(mode.c_str())),
                                      static_cast<ULONG>((mode.size() + 1) * sizeof(wchar_t)), 0))) {
                BCryptCloseAlgorithmProvider(opened.aes, 0);
                opened.aes = nullptr;
            }
        } else {
            opened.aes = nullptr;
        }
        if (!ok(BCryptOpenAlgorithmProvider(&opened.sha256, BCRYPT_SHA256_ALGORITHM, nullptr, 0))) {
            opened.sha256 = nullptr;
        }
        return opened;
    }();
    return shared;
}

struct KeyHolder {
    BCRYPT_KEY_HANDLE key = nullptr;
    KeyHolder() = default;
    ~KeyHolder() {
        if (key) {
            BCryptDestroyKey(key);
        }
    }
    KeyHolder(const KeyHolder&) = delete;
    KeyHolder& operator=(const KeyHolder&) = delete;
};

void appendDerLength(std::string& out, std::size_t length) {
    if (length < 0x80) {
        out.push_back(static_cast<char>(length));
        return;
    }
    std::string encoded;
    while (length > 0) {
        encoded.insert(encoded.begin(), static_cast<char>(length & 0xff));
        length >>= 8;
    }
    out.push_back(static_cast<char>(0x80 | encoded.size()));
    out += encoded;
}

void appendDer(std::string& out, unsigned char tag, std::string_view body) {
    out.push_back(static_cast<char>(tag));
    appendDerLength(out, body.size());
    out.append(body);
}

void appendDerInteger(std::string& out, std::string_view bigEndian) {
    while (bigEndian.size() > 1 && bigEndian.front() == '\0') {
        bigEndian.remove_prefix(1);
    }
    std::string value;
    if (bigEndian.empty() || (static_cast<unsigned char>(bigEndian.front()) & 0x80) != 0) {
        value.push_back('\0');
    }
    value.append(bigEndian);
    appendDer(out, 0x02, value);
}

std::string subjectPublicKeyInfo(std::string_view modulus, std::string_view exponent) {
    static constexpr unsigned char kRsaAlgorithm[] = {0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86,
                                                      0xf7, 0x0d, 0x01, 0x01, 0x01, 0x05, 0x00};
    std::string integers;
    appendDerInteger(integers, modulus);
    appendDerInteger(integers, exponent);
    std::string rsaPublicKey;
    appendDer(rsaPublicKey, 0x30, integers);
    std::string bitString(1, '\0');
    bitString.append(rsaPublicKey);
    std::string body(reinterpret_cast<const char*>(kRsaAlgorithm), sizeof kRsaAlgorithm);
    appendDer(body, 0x03, bitString);
    std::string out;
    appendDer(out, 0x30, body);
    return out;
}

bool readDerLength(std::string_view& data, std::size_t& length) {
    if (data.empty()) {
        return false;
    }
    const auto first = static_cast<unsigned char>(data.front());
    data.remove_prefix(1);
    if (first < 0x80) {
        length = first;
        return length <= data.size();
    }
    const std::size_t count = first & 0x7f;
    if (count == 0 || count > sizeof(std::size_t) || count > data.size()) {
        return false;
    }
    length = 0;
    for (std::size_t i = 0; i < count; ++i) {
        length = (length << 8) | static_cast<unsigned char>(data[i]);
    }
    data.remove_prefix(count);
    return length <= data.size();
}

bool readDerInteger(std::string_view& data, std::size_t width, std::string& out) {
    std::size_t length = 0;
    if (data.empty() || data.front() != 0x02) {
        return false;
    }
    data.remove_prefix(1);
    if (!readDerLength(data, length)) {
        return false;
    }
    std::string_view value = data.substr(0, length);
    data.remove_prefix(length);
    while (!value.empty() && value.front() == '\0') {
        value.remove_prefix(1);
    }
    if (value.size() > width) {
        return false;
    }
    out.append(width - value.size(), '\0');
    out.append(value);
    return true;
}

bool ecdsaRawSignature(std::string_view der, std::size_t width, std::string& raw) {
    std::size_t length = 0;
    if (der.empty() || der.front() != 0x30) {
        return false;
    }
    der.remove_prefix(1);
    if (!readDerLength(der, length)) {
        return false;
    }
    std::string_view body = der.substr(0, length);
    raw.clear();
    return readDerInteger(body, width, raw) && readDerInteger(body, width, raw) && body.empty();
}

bool sha256(std::string_view message, unsigned char (&digest)[kSha256Bytes]) {
    BCRYPT_HASH_HANDLE hash = nullptr;
    if (!providers().sha256 || !ok(BCryptCreateHash(providers().sha256, &hash, nullptr, 0, nullptr, 0, 0))) {
        return false;
    }
    const bool done = ok(BCryptHashData(hash, bytes(message), static_cast<ULONG>(message.size()), 0)) &&
                      ok(BCryptFinishHash(hash, digest, kSha256Bytes, 0));
    BCryptDestroyHash(hash);
    return done;
}

struct CertificateHolder {
    PCCERT_CONTEXT context = nullptr;
    CertificateHolder() = default;
    ~CertificateHolder() {
        if (context) {
            CertFreeCertificateContext(context);
        }
    }
    CertificateHolder(const CertificateHolder&) = delete;
    CertificateHolder& operator=(const CertificateHolder&) = delete;
};

struct StoreHolder {
    HCERTSTORE store = nullptr;
    StoreHolder() = default;
    ~StoreHolder() {
        if (store) {
            CertCloseStore(store, 0);
        }
    }
    StoreHolder(const StoreHolder&) = delete;
    StoreHolder& operator=(const StoreHolder&) = delete;
};

std::string commonName(PCCERT_CONTEXT certificate) {
    wchar_t name[256];
    DWORD length = CertGetNameStringW(certificate, CERT_NAME_ATTR_TYPE, 0, const_cast<char*>(szOID_COMMON_NAME), name,
                                      sizeof name / sizeof name[0]);
    if (length <= 1) {
        length = CertGetNameStringW(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr, name,
                                    sizeof name / sizeof name[0]);
    }
    return length > 1 ? windows_text::utf8(std::wstring_view(name, length - 1)) : std::string();
}

bool chainTrusted(PCCERT_CONTEXT leaf, HCERTSTORE intermediates, const std::string& host) {
    CERT_CHAIN_PARA chainPara{};
    chainPara.cbSize = sizeof chainPara;
    PCCERT_CHAIN_CONTEXT chain = nullptr;
    if (!CertGetCertificateChain(nullptr, leaf, nullptr, intermediates, &chainPara, 0, nullptr, &chain) || !chain) {
        return false;
    }
    std::wstring serverName = windows_text::wide(host);
    SSL_EXTRA_CERT_CHAIN_POLICY_PARA extra{};
    extra.cbSize = sizeof extra;
    extra.dwAuthType = AUTHTYPE_SERVER;
    extra.pwszServerName = serverName.data();
    CERT_CHAIN_POLICY_PARA policyPara{};
    policyPara.cbSize = sizeof policyPara;
    policyPara.pvExtraPolicyPara = &extra;
    CERT_CHAIN_POLICY_STATUS status{};
    status.cbSize = sizeof status;
    const bool checked = CertVerifyCertificateChainPolicy(CERT_CHAIN_POLICY_SSL, chain, &policyPara, &status);
    CertFreeCertificateChain(chain);
    return checked && status.dwError == 0;
}

} // namespace

bool TcpCrypto::verifyServer(std::string_view proof, std::string_view encryptedKey, const std::string& host,
                             bool& trusted, std::string& subject, std::string& error) const {
    trusted = false;
    subject.clear();
    TcpServerProof parsed;
    if (!parseTcpServerProof(proof, parsed) || parsed.certificates.empty()) {
        error = "the server proof is malformed";
        return false;
    }
    StoreHolder intermediates;
    intermediates.store = CertOpenStore(CERT_STORE_PROV_MEMORY, 0, 0, CERT_STORE_CREATE_NEW_FLAG, nullptr);
    if (!intermediates.store) {
        error = "cannot check the server certificate";
        return false;
    }
    CertificateHolder leaf;
    for (std::size_t i = 0; i < parsed.certificates.size(); ++i) {
        const std::string& der = parsed.certificates[i];
        PCCERT_CONTEXT added = nullptr;
        if (!CertAddEncodedCertificateToStore(intermediates.store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                              bytes(der), static_cast<DWORD>(der.size()), CERT_STORE_ADD_ALWAYS,
                                              i == 0 ? &added : nullptr)) {
            error = "the server certificate does not parse";
            return false;
        }
        if (i == 0) {
            leaf.context = added;
        }
    }
    subject = commonName(leaf.context);
    const char* keyType = leaf.context->pCertInfo->SubjectPublicKeyInfo.Algorithm.pszObjId;
    const bool rsa = keyType && std::strcmp(keyType, szOID_RSA_RSA) == 0;
    const bool ec = keyType && std::strcmp(keyType, szOID_ECC_PUBLIC_KEY) == 0;
    if (!rsa && !ec) {
        error = "the server key type is not RSA or EC";
        return false;
    }
    KeyHolder key;
    if (!CryptImportPublicKeyInfoEx2(X509_ASN_ENCODING, &leaf.context->pCertInfo->SubjectPublicKeyInfo, 0, nullptr,
                                     &key.key)) {
        error = "the server certificate has no usable key: " + windows_text::errorText(GetLastError());
        return false;
    }
    unsigned char digest[kSha256Bytes];
    if (!sha256(tcpHandshakeSignedMessage(publicKey_, encryptedKey), digest)) {
        error = "cannot hash the handshake";
        return false;
    }
    NTSTATUS status = 0;
    if (rsa) {
        BCRYPT_PKCS1_PADDING_INFO padding{BCRYPT_SHA256_ALGORITHM};
        status = BCryptVerifySignature(key.key, &padding, digest, kSha256Bytes, bytes(parsed.signature),
                                       static_cast<ULONG>(parsed.signature.size()), BCRYPT_PAD_PKCS1);
    } else {
        DWORD bits = 0;
        ULONG written = 0;
        std::string raw;
        if (!ok(BCryptGetProperty(key.key, BCRYPT_KEY_LENGTH, reinterpret_cast<PUCHAR>(&bits), sizeof bits, &written,
                                  0)) ||
            !ecdsaRawSignature(parsed.signature, (bits + 7) / 8, raw)) {
            error = "the handshake signature does not match: bad ECDSA signature";
            return false;
        }
        status = BCryptVerifySignature(key.key, nullptr, digest, kSha256Bytes, bytes(raw),
                                       static_cast<ULONG>(raw.size()), 0);
    }
    if (!ok(status)) {
        error = "the handshake signature does not match: " + statusText(status);
        return false;
    }
    trusted = chainTrusted(leaf.context, intermediates.store, host);
    return true;
}

TcpCrypto::~TcpCrypto() {
    clear();
}

void TcpCrypto::releasePrivateKey() {
    if (privateKey_) {
        BCryptDestroyKey(static_cast<BCRYPT_KEY_HANDLE>(privateKey_));
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
    if (!providers().rsa) {
        error = "no RSA provider";
        return false;
    }
    BCRYPT_KEY_HANDLE key = nullptr;
    NTSTATUS status = BCryptGenerateKeyPair(providers().rsa, &key, kRsaBits, 0);
    if (ok(status)) {
        status = BCryptFinalizeKeyPair(key, 0);
    }
    if (!ok(status)) {
        if (key) {
            BCryptDestroyKey(key);
        }
        error = "cannot create the RSA key: " + statusText(status);
        return false;
    }
    ULONG size = 0;
    status = BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, nullptr, 0, &size, 0);
    std::string blob(size, '\0');
    if (ok(status)) {
        status = BCryptExportKey(key, nullptr, BCRYPT_RSAPUBLIC_BLOB, bytes(blob), size, &size, 0);
    }
    BCRYPT_RSAKEY_BLOB header{};
    if (ok(status) && size >= sizeof header) {
        std::memcpy(&header, blob.data(), sizeof header);
    }
    if (!ok(status) || size < sizeof header || sizeof header + header.cbPublicExp + header.cbModulus > size) {
        BCryptDestroyKey(key);
        error = "cannot export the RSA public key: " + statusText(status);
        return false;
    }
    const std::string_view data(blob.data(), size);
    const std::string_view exponent = data.substr(sizeof header, header.cbPublicExp);
    const std::string_view modulus = data.substr(sizeof header + header.cbPublicExp, header.cbModulus);
    publicKey_ = subjectPublicKeyInfo(modulus, exponent);
    privateKey_ = key;
    return true;
}

bool TcpCrypto::acceptSessionKey(std::string_view encryptedKey, std::string& error) {
    wipe(sessionKey_);
    if (!privateKey_) {
        error = "no RSA key";
        return false;
    }
    std::string plain(kRsaBits / 8, '\0');
    ULONG moved = 0;
    const NTSTATUS status =
        BCryptDecrypt(static_cast<BCRYPT_KEY_HANDLE>(privateKey_), bytes(encryptedKey),
                      static_cast<ULONG>(encryptedKey.size()), nullptr, nullptr, 0, bytes(plain),
                      static_cast<ULONG>(plain.size()), &moved, BCRYPT_PAD_PKCS1);
    releasePrivateKey();
    if (!ok(status)) {
        wipe(plain);
        error = "cannot decrypt the session key: " + statusText(status);
        return false;
    }
    if (moved == kSessionKeyBytes) {
        sessionKey_.assign(plain.data(), moved);
    }
    wipe(plain);
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
    KeyHolder key;
    if (!providers().aes || !ok(BCryptGenerateSymmetricKey(providers().aes, &key.key, nullptr, 0, bytes(sessionKey_),
                                                           static_cast<ULONG>(sessionKey_.size()), 0))) {
        error = "cannot load the AES key";
        return false;
    }
    out.assign(kIvBytes + plain.size() + kIvBytes, '\0');
    if (!ok(BCryptGenRandom(nullptr, bytes(out), kIvBytes, BCRYPT_USE_SYSTEM_PREFERRED_RNG))) {
        wipe(out);
        error = "cannot make a random IV";
        return false;
    }
    unsigned char iv[kIvBytes];
    std::memcpy(iv, out.data(), kIvBytes);
    ULONG moved = 0;
    const NTSTATUS status =
        BCryptEncrypt(key.key, bytes(plain), static_cast<ULONG>(plain.size()), nullptr, iv, kIvBytes,
                      reinterpret_cast<PUCHAR>(out.data() + kIvBytes), static_cast<ULONG>(out.size() - kIvBytes),
                      &moved, BCRYPT_BLOCK_PADDING);
    if (!ok(status)) {
        wipe(out);
        error = "AES encrypt failed: " + statusText(status);
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
    if (data.size() <= kIvBytes || (data.size() - kIvBytes) % kIvBytes != 0) {
        error = "the encrypted content has a bad size";
        return false;
    }
    KeyHolder key;
    if (!providers().aes || !ok(BCryptGenerateSymmetricKey(providers().aes, &key.key, nullptr, 0, bytes(sessionKey_),
                                                           static_cast<ULONG>(sessionKey_.size()), 0))) {
        error = "cannot load the AES key";
        return false;
    }
    unsigned char iv[kIvBytes];
    std::memcpy(iv, data.data(), kIvBytes);
    const std::string_view cipher = data.substr(kIvBytes);
    out.assign(cipher.size(), '\0');
    ULONG moved = 0;
    const NTSTATUS status = BCryptDecrypt(key.key, bytes(cipher), static_cast<ULONG>(cipher.size()), nullptr, iv,
                                          kIvBytes, bytes(out), static_cast<ULONG>(out.size()), &moved,
                                          BCRYPT_BLOCK_PADDING);
    if (!ok(status)) {
        wipe(out);
        error = "AES decrypt failed: " + statusText(status);
        return false;
    }
    out.resize(moved);
    return true;
}

} // namespace vibe::net

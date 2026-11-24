#include "vibe/net/transport/TcpCrypto.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/rsa.h>
#include <openssl/x509.h>
#include <openssl/x509_vfy.h>
#include <openssl/x509v3.h>

#include <algorithm>
#include <climits>
#include <memory>

#include "vibe/net/TcpCodec.h"

namespace vibe::net {
namespace {

constexpr int kRsaBits = 2048;
constexpr std::size_t kSessionKeyBytes = 32;
constexpr std::size_t kIvBytes = 16;

void wipe(std::string& secret) {
    OPENSSL_cleanse(secret.data(), secret.size());
    secret.clear();
}

std::string opensslError() {
    const unsigned long code = ERR_get_error();
    ERR_clear_error();
    if (code == 0) {
        return "unknown error";
    }
    char text[256];
    ERR_error_string_n(code, text, sizeof text);
    return text;
}

const unsigned char* bytes(std::string_view data) {
    return reinterpret_cast<const unsigned char*>(data.data());
}

unsigned char* bytes(std::string& data) {
    return reinterpret_cast<unsigned char*>(data.data());
}

struct PkeyFree {
    void operator()(EVP_PKEY* key) const { EVP_PKEY_free(key); }
};
struct PkeyCtxFree {
    void operator()(EVP_PKEY_CTX* ctx) const { EVP_PKEY_CTX_free(ctx); }
};
struct CipherCtxFree {
    void operator()(EVP_CIPHER_CTX* ctx) const { EVP_CIPHER_CTX_free(ctx); }
};
struct MdCtxFree {
    void operator()(EVP_MD_CTX* ctx) const { EVP_MD_CTX_free(ctx); }
};
struct X509Free {
    void operator()(X509* certificate) const { X509_free(certificate); }
};
struct StackFree {
    void operator()(STACK_OF(X509)* stack) const { sk_X509_pop_free(stack, X509_free); }
};
struct StoreFree {
    void operator()(X509_STORE* store) const { X509_STORE_free(store); }
};
struct StoreCtxFree {
    void operator()(X509_STORE_CTX* ctx) const { X509_STORE_CTX_free(ctx); }
};

using Pkey = std::unique_ptr<EVP_PKEY, PkeyFree>;
using PkeyCtx = std::unique_ptr<EVP_PKEY_CTX, PkeyCtxFree>;
using CipherCtx = std::unique_ptr<EVP_CIPHER_CTX, CipherCtxFree>;
using MdCtx = std::unique_ptr<EVP_MD_CTX, MdCtxFree>;
using Certificate = std::unique_ptr<X509, X509Free>;
using CertificateStack = std::unique_ptr<STACK_OF(X509), StackFree>;
using Store = std::unique_ptr<X509_STORE, StoreFree>;
using StoreCtx = std::unique_ptr<X509_STORE_CTX, StoreCtxFree>;

Certificate parseCertificate(const std::string& der) {
    if (der.size() > static_cast<std::size_t>(LONG_MAX)) {
        return nullptr;
    }
    const unsigned char* cursor = bytes(der);
    Certificate certificate(d2i_X509(nullptr, &cursor, static_cast<long>(der.size())));
    if (certificate && cursor != bytes(der) + der.size()) {
        certificate.reset();
    }
    return certificate;
}

std::string commonName(X509* certificate) {
    X509_NAME* name = X509_get_subject_name(certificate);
    if (!name) {
        return {};
    }
    const int index = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (index < 0) {
        return {};
    }
    ASN1_STRING* value = X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, index));
    unsigned char* text = nullptr;
    const int length = value ? ASN1_STRING_to_UTF8(&text, value) : -1;
    if (length < 0 || !text) {
        return {};
    }
    std::string out(reinterpret_cast<const char*>(text), static_cast<std::size_t>(length));
    OPENSSL_free(text);
    return out;
}

bool chainTrusted(X509* leaf, STACK_OF(X509)* intermediates, const std::string& host) {
    Store store(X509_STORE_new());
    StoreCtx ctx(X509_STORE_CTX_new());
    if (!store || !ctx || X509_STORE_set_default_paths(store.get()) != 1 ||
        X509_STORE_CTX_init(ctx.get(), store.get(), leaf, intermediates) != 1 ||
        X509_STORE_CTX_set_purpose(ctx.get(), X509_PURPOSE_SSL_SERVER) != 1) {
        ERR_clear_error();
        return false;
    }
    X509_VERIFY_PARAM* param = X509_STORE_CTX_get0_param(ctx.get());
    if (X509_VERIFY_PARAM_set1_ip_asc(param, host.c_str()) != 1) {
        ERR_clear_error();
        X509_VERIFY_PARAM_set_hostflags(param, X509_CHECK_FLAG_NO_PARTIAL_WILDCARDS);
        if (host.empty() || X509_VERIFY_PARAM_set1_host(param, host.c_str(), host.size()) != 1) {
            ERR_clear_error();
            return false;
        }
    }
    const bool trusted = X509_verify_cert(ctx.get()) == 1;
    ERR_clear_error();
    return trusted;
}

bool runCipher(bool encrypting, std::string_view key, const unsigned char* iv, std::string_view in, std::string& out,
               std::string& error) {
    CipherCtx ctx(EVP_CIPHER_CTX_new());
    if (!ctx || in.size() > static_cast<std::size_t>(INT_MAX - static_cast<int>(kIvBytes)) ||
        EVP_CipherInit_ex(ctx.get(), EVP_aes_256_cbc(), nullptr, bytes(key), iv, encrypting ? 1 : 0) != 1) {
        error = "cannot load the AES key: " + opensslError();
        return false;
    }
    std::string buffer(in.size() + kIvBytes, '\0');
    int written = 0;
    int last = 0;
    if (EVP_CipherUpdate(ctx.get(), bytes(buffer), &written, bytes(in), static_cast<int>(in.size())) != 1 ||
        EVP_CipherFinal_ex(ctx.get(), bytes(buffer) + written, &last) != 1) {
        wipe(buffer);
        error = std::string(encrypting ? "AES encrypt failed: " : "AES decrypt failed: ") + opensslError();
        return false;
    }
    buffer.resize(static_cast<std::size_t>(written + last));
    out.append(buffer);
    wipe(buffer);
    return true;
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
    Certificate leaf = parseCertificate(parsed.certificates.front());
    CertificateStack intermediates(sk_X509_new_null());
    if (!leaf || !intermediates) {
        ERR_clear_error();
        error = "the server certificate does not parse";
        return false;
    }
    for (std::size_t i = 1; i < parsed.certificates.size(); ++i) {
        Certificate certificate = parseCertificate(parsed.certificates[i]);
        if (!certificate || sk_X509_push(intermediates.get(), certificate.get()) <= 0) {
            ERR_clear_error();
            error = "the server certificate does not parse";
            return false;
        }
        certificate.release();
    }
    subject = commonName(leaf.get());
    EVP_PKEY* key = X509_get0_pubkey(leaf.get());
    const int keyType = key ? EVP_PKEY_get_base_id(key) : EVP_PKEY_NONE;
    if (keyType != EVP_PKEY_RSA && keyType != EVP_PKEY_EC) {
        ERR_clear_error();
        error = "the server key type is not RSA or EC";
        return false;
    }
    const std::string message = tcpHandshakeSignedMessage(publicKey_, encryptedKey);
    MdCtx md(EVP_MD_CTX_new());
    EVP_PKEY_CTX* keyCtx = nullptr;
    if (!md || EVP_DigestVerifyInit(md.get(), &keyCtx, EVP_sha256(), nullptr, key) != 1 ||
        (keyType == EVP_PKEY_RSA && EVP_PKEY_CTX_set_rsa_padding(keyCtx, RSA_PKCS1_PADDING) != 1)) {
        error = "cannot check the handshake signature: " + opensslError();
        return false;
    }
    if (EVP_DigestVerify(md.get(), bytes(parsed.signature), parsed.signature.size(), bytes(message),
                         message.size()) != 1) {
        error = "the handshake signature does not match: " + opensslError();
        return false;
    }
    trusted = chainTrusted(leaf.get(), intermediates.get(), host);
    return true;
}

TcpCrypto::~TcpCrypto() {
    clear();
}

void TcpCrypto::releasePrivateKey() {
    if (privateKey_) {
        EVP_PKEY_free(static_cast<EVP_PKEY*>(privateKey_));
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
    PkeyCtx ctx(EVP_PKEY_CTX_new_id(EVP_PKEY_RSA, nullptr));
    EVP_PKEY* generated = nullptr;
    if (!ctx || EVP_PKEY_keygen_init(ctx.get()) != 1 || EVP_PKEY_CTX_set_rsa_keygen_bits(ctx.get(), kRsaBits) != 1 ||
        EVP_PKEY_keygen(ctx.get(), &generated) != 1) {
        error = "cannot create the RSA key: " + opensslError();
        return false;
    }
    Pkey key(generated);
    unsigned char* der = nullptr;
    const int length = i2d_PUBKEY(key.get(), &der);
    if (length <= 0 || !der) {
        error = "cannot export the RSA public key: " + opensslError();
        return false;
    }
    publicKey_.assign(reinterpret_cast<const char*>(der), static_cast<std::size_t>(length));
    OPENSSL_free(der);
    privateKey_ = key.release();
    return true;
}

bool TcpCrypto::acceptSessionKey(std::string_view encryptedKey, std::string& error) {
    wipe(sessionKey_);
    if (!privateKey_) {
        error = "no RSA key";
        return false;
    }
    PkeyCtx ctx(EVP_PKEY_CTX_new(static_cast<EVP_PKEY*>(privateKey_), nullptr));
    std::string plain(kRsaBits / 8, '\0');
    std::size_t moved = plain.size();
    const bool decrypted = ctx && EVP_PKEY_decrypt_init(ctx.get()) == 1 &&
                           EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING) == 1 &&
                           EVP_PKEY_decrypt(ctx.get(), bytes(plain), &moved, bytes(encryptedKey),
                                            encryptedKey.size()) == 1;
    ctx.reset();
    releasePrivateKey();
    if (!decrypted) {
        wipe(plain);
        error = "cannot decrypt the session key: " + opensslError();
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
    out.clear();
    if (sessionKey_.empty()) {
        error = "no session key";
        return false;
    }
    unsigned char iv[kIvBytes];
    if (RAND_bytes(iv, sizeof iv) != 1) {
        error = "cannot make a random IV: " + opensslError();
        return false;
    }
    out.assign(reinterpret_cast<const char*>(iv), kIvBytes);
    if (!runCipher(true, sessionKey_, iv, plain, out, error)) {
        wipe(out);
        return false;
    }
    return true;
}

bool TcpCrypto::decrypt(std::string_view data, std::string& out, std::string& error) const {
    out.clear();
    if (sessionKey_.empty()) {
        error = "no session key";
        return false;
    }
    if (data.size() <= kIvBytes || (data.size() - kIvBytes) % kIvBytes != 0) {
        error = "the encrypted content has a bad size";
        return false;
    }
    unsigned char iv[kIvBytes];
    std::copy_n(bytes(data), kIvBytes, iv);
    if (!runCipher(false, sessionKey_, iv, data.substr(kIvBytes), out, error)) {
        wipe(out);
        return false;
    }
    return true;
}

} // namespace vibe::net

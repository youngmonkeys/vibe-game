#include "vibe/net/transport/TcpCrypto.h"

#include <stdlib.h>

#include <algorithm>
#include <cstdint>

#include "vibe/net/TcpCodec.h"
#include "vibe/net/transport/android/Jni.h"

namespace vibe::net {
namespace {

constexpr std::size_t kSessionKeyBytes = 32;
constexpr std::size_t kIvBytes = 16;

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

bool aes(bool encrypt, const std::string& key, std::string_view iv, std::string_view data, std::string& out,
         std::string& error) {
    JNIEnv* env = jni::env();
    jclass crypto = env ? jni::cryptoClass() : nullptr;
    if (!crypto) {
        error = "no Java crypto class";
        return false;
    }
    jni::LocalFrame frame(env, 8);
    static jmethodID method = env->GetStaticMethodID(crypto, "aes", "(Z[B[B[B)[B");
    if (!method || jni::failed(env, error)) {
        return false;
    }
    auto result = static_cast<jbyteArray>(env->CallStaticObjectMethod(
        crypto, method, static_cast<jboolean>(encrypt), jni::newBytes(env, key), jni::newBytes(env, iv),
        jni::newBytes(env, data)));
    if (jni::failed(env, error) || !result) {
        if (error.empty()) {
            error = "no result";
        }
        return false;
    }
    out = jni::takeBytes(env, result);
    return true;
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
    JNIEnv* env = jni::env();
    jclass crypto = env ? jni::cryptoClass() : nullptr;
    if (!crypto) {
        error = "no Java crypto class";
        return false;
    }
    jni::LocalFrame frame(env, 16 + static_cast<jint>(parsed.certificates.size()));
    static jmethodID method =
        env->GetStaticMethodID(crypto, "verify", "([[B[B[BLjava/lang/String;)[Ljava/lang/String;");
    if (!method || jni::failed(env, error)) {
        return false;
    }
    jclass bytesClass = env->FindClass("[B");
    jobjectArray chain = env->NewObjectArray(static_cast<jsize>(parsed.certificates.size()), bytesClass, nullptr);
    for (std::size_t i = 0; i < parsed.certificates.size(); ++i) {
        env->SetObjectArrayElement(chain, static_cast<jsize>(i), jni::newBytes(env, parsed.certificates[i]));
    }
    auto result = static_cast<jobjectArray>(
        env->CallStaticObjectMethod(crypto, method, chain,
                                    jni::newBytes(env, tcpHandshakeSignedMessage(publicKey_, encryptedKey)),
                                    jni::newBytes(env, parsed.signature), env->NewStringUTF(host.c_str())));
    if (jni::failed(env, error) || !result || env->GetArrayLength(result) < 3) {
        if (error.empty()) {
            error = "cannot check the server certificate";
        }
        return false;
    }
    const std::string failure = jni::takeString(env, static_cast<jstring>(env->GetObjectArrayElement(result, 0)));
    subject = jni::takeString(env, static_cast<jstring>(env->GetObjectArrayElement(result, 2)));
    if (!failure.empty()) {
        error = failure;
        return false;
    }
    trusted = jni::takeString(env, static_cast<jstring>(env->GetObjectArrayElement(result, 1))) == "1";
    return true;
}

TcpCrypto::~TcpCrypto() {
    clear();
}

void TcpCrypto::releasePrivateKey() {
    if (privateKey_) {
        if (JNIEnv* env = jni::env()) {
            env->DeleteGlobalRef(static_cast<jobject>(privateKey_));
        }
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
    JNIEnv* env = jni::env();
    jclass crypto = env ? jni::cryptoClass() : nullptr;
    if (!crypto) {
        error = "no Java crypto class";
        return false;
    }
    jni::LocalFrame frame(env, 8);
    static jmethodID generateMethod = env->GetStaticMethodID(crypto, "generate", "()Ljava/security/KeyPair;");
    static jmethodID publicKeyMethod =
        env->GetStaticMethodID(crypto, "publicKey", "(Ljava/security/KeyPair;)[B");
    if (!generateMethod || !publicKeyMethod || jni::failed(env, error)) {
        return false;
    }
    jobject keyPair = env->CallStaticObjectMethod(crypto, generateMethod);
    if (jni::failed(env, error) || !keyPair) {
        error = "cannot create the RSA key: " + error;
        return false;
    }
    auto encoded = static_cast<jbyteArray>(env->CallStaticObjectMethod(crypto, publicKeyMethod, keyPair));
    if (jni::failed(env, error) || !encoded) {
        error = "cannot export the RSA public key: " + error;
        return false;
    }
    publicKey_ = jni::takeBytes(env, encoded);
    privateKey_ = env->NewGlobalRef(keyPair);
    return true;
}

bool TcpCrypto::acceptSessionKey(std::string_view encryptedKey, std::string& error) {
    wipe(sessionKey_);
    if (!privateKey_) {
        error = "no RSA key";
        return false;
    }
    JNIEnv* env = jni::env();
    jclass crypto = env ? jni::cryptoClass() : nullptr;
    if (!crypto) {
        error = "no Java crypto class";
        return false;
    }
    std::string key;
    {
        jni::LocalFrame frame(env, 8);
        static jmethodID method = env->GetStaticMethodID(crypto, "decrypt", "(Ljava/security/KeyPair;[B)[B");
        if (!method || jni::failed(env, error)) {
            return false;
        }
        auto plain = static_cast<jbyteArray>(env->CallStaticObjectMethod(
            crypto, method, static_cast<jobject>(privateKey_), jni::newBytes(env, encryptedKey)));
        if (jni::failed(env, error) || !plain) {
            releasePrivateKey();
            error = "cannot decrypt the session key: " + error;
            return false;
        }
        key = jni::takeBytes(env, plain);
    }
    releasePrivateKey();
    if (key.size() == kSessionKeyBytes) {
        sessionKey_ = key;
    }
    wipe(key);
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
    std::string iv(kIvBytes, '\0');
    arc4random_buf(iv.data(), iv.size());
    std::string cipher;
    if (!aes(true, sessionKey_, iv, plain, cipher, error)) {
        error = "AES encrypt failed: " + error;
        return false;
    }
    out = iv + cipher;
    wipe(cipher);
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
    if (!aes(false, sessionKey_, data.substr(0, kIvBytes), data.substr(kIvBytes), out, error)) {
        wipe(out);
        error = "AES decrypt failed: " + error;
        return false;
    }
    return true;
}

} // namespace vibe::net

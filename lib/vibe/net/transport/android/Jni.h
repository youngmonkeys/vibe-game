#pragma once

#include <jni.h>

#include <string>
#include <string_view>

namespace vibe::net::jni {

JNIEnv* env();

jclass httpClass();
jclass cryptoClass();

class LocalFrame {
public:
    LocalFrame(JNIEnv* env, jint capacity);
    ~LocalFrame();
    LocalFrame(const LocalFrame&) = delete;
    LocalFrame& operator=(const LocalFrame&) = delete;

private:
    JNIEnv* env_;
    bool pushed_;
};

jbyteArray newBytes(JNIEnv* env, std::string_view bytes);
std::string takeBytes(JNIEnv* env, jbyteArray array);
std::string takeString(JNIEnv* env, jstring text);
bool failed(JNIEnv* env, std::string& error);

} // namespace vibe::net::jni

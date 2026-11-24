#include "vibe/net/transport/android/Jni.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_system.h>

#include <algorithm>
#include <vector>

namespace vibe::net::jni {
namespace {

jclass loadAppClass(const char* name) {
    JNIEnv* e = env();
    if (!e) {
        return nullptr;
    }
    LocalFrame frame(e, 16);
    jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
    if (!activity) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "jni: no activity to load %s: %s", name, SDL_GetError());
        return nullptr;
    }
    jclass activityClass = e->GetObjectClass(activity);
    jmethodID getClassLoader = e->GetMethodID(activityClass, "getClassLoader", "()Ljava/lang/ClassLoader;");
    jobject loader = getClassLoader ? e->CallObjectMethod(activity, getClassLoader) : nullptr;
    jclass loaderClass = e->FindClass("java/lang/ClassLoader");
    jmethodID loadClass =
        loaderClass ? e->GetMethodID(loaderClass, "loadClass", "(Ljava/lang/String;)Ljava/lang/Class;") : nullptr;
    jobject found = (loader && loadClass) ? e->CallObjectMethod(loader, loadClass, e->NewStringUTF(name)) : nullptr;
    std::string error;
    if (failed(e, error) || !found) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "jni: cannot load %s: %s", name, error.c_str());
        return nullptr;
    }
    e->DeleteLocalRef(activity);
    return static_cast<jclass>(e->NewGlobalRef(found));
}

} // namespace

JNIEnv* env() {
    auto* e = static_cast<JNIEnv*>(SDL_GetAndroidJNIEnv());
    if (!e) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "jni: no JNIEnv on this thread: %s", SDL_GetError());
    }
    return e;
}

jclass httpClass() {
    static jclass loaded = loadAppClass("org.youngmonkeys.vibe.VibeHttp");
    return loaded;
}

jclass cryptoClass() {
    static jclass loaded = loadAppClass("org.youngmonkeys.vibe.VibeCrypto");
    return loaded;
}

LocalFrame::LocalFrame(JNIEnv* env, jint capacity) : env_(env), pushed_(env->PushLocalFrame(capacity) == 0) {}

LocalFrame::~LocalFrame() {
    if (pushed_) {
        env_->PopLocalFrame(nullptr);
    }
}

jbyteArray newBytes(JNIEnv* env, std::string_view bytes) {
    jbyteArray array = env->NewByteArray(static_cast<jsize>(bytes.size()));
    if (array && !bytes.empty()) {
        env->SetByteArrayRegion(array, 0, static_cast<jsize>(bytes.size()),
                                reinterpret_cast<const jbyte*>(bytes.data()));
    }
    return array;
}

std::string takeBytes(JNIEnv* env, jbyteArray array) {
    std::string out;
    if (!array) {
        return out;
    }
    const jsize length = env->GetArrayLength(array);
    out.resize(static_cast<std::size_t>(length));
    if (length > 0) {
        env->GetByteArrayRegion(array, 0, length, reinterpret_cast<jbyte*>(out.data()));
        std::vector<jbyte> zeros(static_cast<std::size_t>(length), 0);
        env->SetByteArrayRegion(array, 0, length, zeros.data());
    }
    return out;
}

std::string takeString(JNIEnv* env, jstring text) {
    if (!text) {
        return {};
    }
    const char* chars = env->GetStringUTFChars(text, nullptr);
    std::string out = chars ? chars : "";
    if (chars) {
        env->ReleaseStringUTFChars(text, chars);
    }
    return out;
}

bool failed(JNIEnv* env, std::string& error) {
    if (!env->ExceptionCheck()) {
        return false;
    }
    jthrowable thrown = env->ExceptionOccurred();
    env->ExceptionClear();
    error = "Java exception";
    if (thrown) {
        jclass throwableClass = env->FindClass("java/lang/Throwable");
        jmethodID toString =
            throwableClass ? env->GetMethodID(throwableClass, "toString", "()Ljava/lang/String;") : nullptr;
        if (toString) {
            auto text = static_cast<jstring>(env->CallObjectMethod(thrown, toString));
            if (env->ExceptionCheck()) {
                env->ExceptionClear();
            } else if (text) {
                error = takeString(env, text);
                env->DeleteLocalRef(text);
            }
        }
        if (throwableClass) {
            env->DeleteLocalRef(throwableClass);
        }
        env->DeleteLocalRef(thrown);
    }
    return true;
}

} // namespace vibe::net::jni

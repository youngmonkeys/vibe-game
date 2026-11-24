#include "vibe/net/transport/HttpRequest.h"

#include <SDL3/SDL_init.h>
#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cstdint>
#include <memory>

#include "vibe/net/transport/android/Jni.h"

namespace vibe::net {
namespace {

struct Answer {
    HttpClient::Request* request = nullptr;
    int status = 0;
    std::string body;
};

void SDLCALL completeOnMainThread(void* userdata) {
    std::unique_ptr<Answer> answer(static_cast<Answer*>(userdata));
    completeRequest(answer->request, answer->status, answer->body.data(), answer->body.size());
    std::fill(answer->body.begin(), answer->body.end(), '\0');
}

void post(Answer* answer) {
    if (!SDL_RunOnMainThread(completeOnMainThread, answer, false)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: cannot hand an answer to the main thread: %s",
                     SDL_GetError());
        std::fill(answer->body.begin(), answer->body.end(), '\0');
        delete answer;
    }
}

} // namespace

namespace http_backend {

void send(HttpClient::Request* request, const char* method) {
    JNIEnv* env = jni::env();
    jclass http = env ? jni::httpClass() : nullptr;
    if (!http) {
        post(new Answer{request, 0, {}});
        return;
    }
    jni::LocalFrame frame(env, 16 + static_cast<jint>(request->headers.size()));
    static jmethodID sendMethod =
        env->GetStaticMethodID(http, "send", "(JLjava/lang/String;Ljava/lang/String;[Ljava/lang/String;[BI)V");
    std::string error;
    if (!sendMethod || jni::failed(env, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: EzyHttp.send not found: %s", error.c_str());
        post(new Answer{request, 0, {}});
        return;
    }
    const std::size_t pairs = request->headers.size() / 2;
    jclass stringClass = env->FindClass("java/lang/String");
    jobjectArray headers = env->NewObjectArray(static_cast<jsize>(pairs * 2), stringClass, nullptr);
    for (std::size_t i = 0; i < pairs * 2; ++i) {
        env->SetObjectArrayElement(headers, static_cast<jsize>(i), env->NewStringUTF(request->headers[i].c_str()));
    }
    jbyteArray body = request->body.empty() ? nullptr : jni::newBytes(env, request->body);
    env->CallStaticVoidMethod(http, sendMethod, static_cast<jlong>(reinterpret_cast<std::intptr_t>(request)),
                              env->NewStringUTF(method), env->NewStringUTF(request->url.c_str()), headers, body,
                              static_cast<jint>(HttpClient::kTimeoutMs));
    if (jni::failed(env, error)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "http: EzyHttp.send failed: %s", error.c_str());
        post(new Answer{request, 0, {}});
    }
}

} // namespace http_backend
} // namespace vibe::net

extern "C" JNIEXPORT void JNICALL Java_org_youngmonkeys_vibe_VibeHttp_nativeDone(JNIEnv* env, jclass,
                                                                                      jlong request, jint status,
                                                                                      jbyteArray body) {
    auto* answer = new vibe::net::Answer{
        reinterpret_cast<vibe::net::HttpClient::Request*>(static_cast<std::intptr_t>(request)), status,
        vibe::net::jni::takeBytes(env, body)};
    vibe::net::post(answer);
}

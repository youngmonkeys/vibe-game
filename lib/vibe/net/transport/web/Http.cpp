#include "vibe/net/transport/HttpRequest.h"

#include <emscripten/fetch.h>

#include <cstdio>

namespace vibe::net {
namespace {

void onFetchDone(emscripten_fetch_t* fetch) {
    auto* request = static_cast<HttpClient::Request*>(fetch->userData);
    completeRequest(request, static_cast<int>(fetch->status), const_cast<char*>(fetch->data),
                    fetch->data ? static_cast<std::size_t>(fetch->numBytes) : 0);
    emscripten_fetch_close(fetch);
}

} // namespace

namespace http_backend {

void send(HttpClient::Request* request, const char* method) {
    for (const auto& header : request->headers) {
        request->headerPointers.push_back(header.c_str());
    }
    request->headerPointers.push_back(nullptr);

    emscripten_fetch_attr_t attr;
    emscripten_fetch_attr_init(&attr);
    std::snprintf(attr.requestMethod, sizeof attr.requestMethod, "%s", method);
    attr.attributes = EMSCRIPTEN_FETCH_LOAD_TO_MEMORY;
    attr.timeoutMSecs = HttpClient::kTimeoutMs;
    attr.onsuccess = onFetchDone;
    attr.onerror = onFetchDone;
    attr.userData = request;
    attr.requestHeaders = request->headerPointers.data();
    if (!request->body.empty()) {
        attr.requestData = request->body.data();
        attr.requestDataSize = request->body.size();
    }
    emscripten_fetch(&attr, request->url.c_str());
}

} // namespace http_backend
} // namespace vibe::net

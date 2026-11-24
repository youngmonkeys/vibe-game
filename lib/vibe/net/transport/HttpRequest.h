#pragma once

#include <cstddef>
#include <string>
#include <vector>

#include "vibe/net/transport/Http.h"

namespace vibe::net {

struct HttpClient::Request {
    HttpClient* owner = nullptr;
    HttpStep step = HttpStep::Login;
    std::string url;
    std::string body;
    std::vector<std::string> headers;
    std::vector<const char*> headerPointers;
    double startedMs = 0.0;
    bool firstPage = true;
};

namespace http_backend {

void send(HttpClient::Request* request, const char* method);

} // namespace http_backend

void completeRequest(HttpClient::Request* request, int status, char* data, std::size_t size);

} // namespace vibe::net

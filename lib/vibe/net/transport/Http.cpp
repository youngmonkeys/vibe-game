#include "vibe/net/transport/Http.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <string_view>

#include "vibe/net/Decode.h"
#include "vibe/net/transport/HttpRequest.h"

namespace vibe::net {
namespace {

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

double nowMs() {
    return static_cast<double>(SDL_GetTicksNS()) / 1e6;
}

} // namespace

void completeRequest(HttpClient::Request* request, int status, char* data, std::size_t size) {
    const double elapsedMs = nowMs() - request->startedMs;
    // Status, size and time only. Never the headers or the body: the login
    // answer holds both tokens.
    if (request->owner) {
        const Protocol& protocol = request->owner->protocol();
        SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "http: %s answered %d, %llu bytes, %.0f ms",
                     httpStepName(protocol, request->step), status, static_cast<unsigned long long>(size), elapsedMs);
        const std::string_view body(data ? data : "", data ? size : 0);
        request->owner->deliver(request,
                                decodeHttpResponse(protocol, request->step, status, body, elapsedMs, request->firstPage));
    }
    // The buffer holds the login answer with its tokens, or the register
    // answer with a password.
    if (data && size > 0) {
        std::fill(data, data + size, '\0');
    }
    wipe(request->body);
    for (auto& header : request->headers) {
        wipe(header);
    }
    delete request;
}

HttpClient::~HttpClient() {
    for (Request* request : pending_) {
        request->owner = nullptr;
    }
}

void HttpClient::deliver(Request* request, InboundMessage&& message) {
    pending_.erase(std::remove(pending_.begin(), pending_.end(), request), pending_.end());
    inbound_.push(std::move(message));
}

void HttpClient::login(const std::string& httpUrl, std::string formBody) {
    auto* request = new Request();
    request->step = HttpFailed::Step::Login;
    request->url = httpUrl + "/ezylogin/api/v1/login";
    request->body = std::move(formBody);
    request->headers = {"Content-Type", "application/x-www-form-urlencoded"};
    start(request, "POST");
}

void HttpClient::gameServers(const std::string& masterUrl, const std::string& accessToken) {
    auto* request = new Request();
    request->step = HttpFailed::Step::GameServers;
    request->url = masterUrl + "/api/v1/game-master-server/game-servers?limit=50";
    // Header `accessToken`, not `Authorization` (research/protocol.md).
    request->headers = {"accessToken", accessToken};
    start(request, "GET");
}

void HttpClient::serverInstances(const std::string& masterUrl, const std::string& accessToken, std::int64_t serverId) {
    auto* request = new Request();
    request->step = HttpFailed::Step::ServerInstances;
    request->url = masterUrl + "/api/v1/game-master-server/game-servers/" + std::to_string(serverId) +
                   "/instances?instanceType=LOBBY&status=AVAILABLE&limit=10";
    request->headers = {"accessToken", accessToken};
    start(request, "GET");
}

void HttpClient::registerAnonymous(const std::string& httpUrl) {
    auto* request = new Request();
    request->step = HttpFailed::Step::Register;
    request->url = httpUrl + "/ezylogin/api/v1/anonymous/register";
    start(request, "POST");
}

void HttpClient::forgotPassword(const std::string& httpUrl, std::string jsonBody) {
    auto* request = new Request();
    request->step = HttpFailed::Step::ForgotPassword;
    request->url = httpUrl + "/ezylogin/api/v1/forgot-password";
    request->body = std::move(jsonBody);
    request->headers = {"Content-Type", "application/json"};
    start(request, "POST");
}

void HttpClient::request(HttpStep step, const char* method, const std::string& url, const std::string& accessToken,
                         std::string jsonBody, bool firstPage) {
    auto* request = new Request();
    request->step = step;
    request->url = url;
    request->firstPage = firstPage;
    request->body = std::move(jsonBody);
    request->headers = {"accessToken", accessToken};
    if (!request->body.empty()) {
        request->headers.push_back("Content-Type");
        request->headers.push_back("application/json");
    }
    start(request, method);
}

bool HttpClient::inFlight(HttpStep step) const {
    return std::any_of(pending_.begin(), pending_.end(),
                       [step](const Request* request) { return request->step == step; });
}

void HttpClient::start(Request* request, const char* method) {
    request->owner = this;
    request->startedMs = nowMs();
    pending_.push_back(request);
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "http: %s %s", method, request->url.c_str());
    http_backend::send(request, method);
}

} // namespace vibe::net

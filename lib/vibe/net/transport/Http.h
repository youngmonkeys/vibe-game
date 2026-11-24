#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "vibe/net/Queues.h"

namespace vibe::net {

// HTTP over the Emscripten Fetch API (decision D15). Asynchronous: a call
// starts the request and returns. The completion callback runs on the main
// thread between frames, decodes the answer with src/net/Decode.h, and pushes
// the result into the inbound queue. Nothing waits on it.
//
// No `no-cors` mode: it would hide the answer. CORS is the server's setting
// (research/ezyfox-websocket.md, Probe log).
class HttpClient {
public:
    // Fetch has no separate connect timeout. A stopped server fails at once
    // with status 0; a silent one fails at this timeout, also status 0.
    static constexpr unsigned kTimeoutMs = 15000;

    explicit HttpClient(InboundQueue& inbound) : inbound_(inbound) {}
    ~HttpClient();
    HttpClient(const HttpClient&) = delete;
    HttpClient& operator=(const HttpClient&) = delete;

    // POST {httpUrl}/ezylogin/api/v1/login as a form. The body (it holds the
    // password) is wiped once the request completes. Pushes HttpLoginOk or
    // HttpFailed.
    void login(const std::string& httpUrl, std::string formBody);

    // GET {masterUrl}/api/v1/game-master-server/game-servers?limit=50 with
    // header `accessToken`. Pushes HttpGameServerList or HttpFailed.
    void gameServers(const std::string& masterUrl, const std::string& accessToken);

    // GET {masterUrl}/api/v1/game-master-server/game-servers/{serverId}/
    // instances?instanceType=LOBBY&status=AVAILABLE with header `accessToken`.
    // Pushes HttpServerInstance (the first one) or HttpFailed.
    void serverInstances(const std::string& masterUrl, const std::string& accessToken, std::int64_t serverId);

    // POST {httpUrl}/ezylogin/api/v1/anonymous/register (quick play).
    // Pushes HttpRegisterOk or HttpFailed.
    void registerAnonymous(const std::string& httpUrl);

    // POST {httpUrl}/ezylogin/api/v1/forgot-password with JSON
    // {"email": ...}, as Unity's ForgotPasswordRequest (plan 11). Pushes
    // HttpForgotPasswordOk or HttpFailed.
    void forgotPassword(const std::string& httpUrl, std::string jsonBody);

    // A request with header `accessToken` for a game step (isGameHttpStep).
    // `jsonBody`, when not empty, is sent as application/json. `firstPage` is
    // passed to the step's Protocol decoder. Pushes that step's message or HttpFailed.
    void request(HttpStep step, const char* method, const std::string& url, const std::string& accessToken,
                 std::string jsonBody = {}, bool firstPage = true);

    // True while a request of this kind has not completed. The completion
    // callback clears it, whether or not its result is still wanted.
    bool inFlight(HttpStep step) const;

    struct Request;
    // Called by the fetch completion callback only.
    void deliver(Request* request, InboundMessage&& message);
    const Protocol& protocol() const { return inbound_.protocol(); }

private:
    void start(Request* request, const char* method);

    InboundQueue& inbound_;
    // In flight. A request outliving this client finishes without pushing.
    std::vector<Request*> pending_;
};

} // namespace vibe::net

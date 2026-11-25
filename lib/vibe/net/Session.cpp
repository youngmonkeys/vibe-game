#include "vibe/net/Session.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_misc.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cstdio>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#include "vibe/net/Encode.h"
#include "vibe/net/Url.h"
#include "vibe/platform/SessionStore.h"

namespace vibe::net {
namespace {

#ifdef VIBE_DEV
// Dev builds warn and continue, so the local http:// server still works.
constexpr bool kRequireSecure = false;
#else
// Release builds use only https:// and wss:// (decision D15).
constexpr bool kRequireSecure = true;
#endif

#if defined(__EMSCRIPTEN__)
constexpr const char* kClientType = "SDL_WEB";
#elif defined(__ANDROID__)
constexpr const char* kClientType = "SDL_ANDROID";
#elif defined(_WIN32)
constexpr const char* kClientType = "SDL_WINDOWS";
#elif defined(__linux__)
constexpr const char* kClientType = "SDL_LINUX";
#elif TARGET_OS_IOS
constexpr const char* kClientType = "SDL_IOS";
#else
constexpr const char* kClientType = "SDL_MACOS";
#endif

void wipe(std::string& secret) {
    std::fill(secret.begin(), secret.end(), '\0');
    secret.clear();
}

// Origin and path, without a query, for log lines.
std::string loggableUrl(const std::string& url) {
    ParsedUrl parsed;
    if (!parseUrl(url, parsed)) {
        return "(unparsable URL)";
    }
    const std::string path = parsed.path.substr(0, parsed.path.find_first_of("?#"));
    return urlOrigin(parsed) + path;
}

// The JS client sends a fresh GUID; any string works (plan 17).
std::string makeClientId() {
    char text[33];
    for (int i = 0; i < 4; ++i) {
        std::snprintf(text + i * 8, 9, "%08x", static_cast<unsigned>(SDL_rand_bits()));
    }
    return std::string(text, 32);
}

const char* errorCommandName(Command command) {
    switch (command) {
    case Command::LoginError: return "zone login";
    case Command::AppAccessError: return "app access";
    case Command::AppRequestError: return "app request";
    default: return "session";
    }
}

} // namespace

Session::Session(SessionConfig config, const Protocol& protocol, SessionListener& listener)
    : config_(std::move(config)), listener_(listener), inbound_(protocol), http_(inbound_),
      socket_(newSocketTransport(inbound_, config_.udpUsage)), udpPort_(config_.udpPort) {}

const char* Session::stateName(State state) {
    switch (state) {
    case State::Idle: return "idle";
    case State::Register: return "anonymous register";
    case State::HttpLogin: return "HTTP login";
    case State::ChooseServer: return "choose server";
    case State::ServerInstance: return "server instance";
    case State::Connecting: return "socket open";
    case State::Handshake: return "handshake";
    case State::ZoneLogin: return "zone login";
    case State::AppAccess: return "app access";
    case State::Ready: return "ready";
    case State::Failed: return "failed";
    }
    return "?";
}

bool Session::login(std::string_view username, std::string& password) {
    if (state_ != State::Idle && state_ != State::Failed) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: login while %s; ignored", stateName(state_));
        wipe(password);
        return false;
    }
    std::string problem;
    const bool allowed =
        checkLoginUrls(config_.httpUrl, config_.socketUrl, kRequireSecure, problem);
    if (!allowed) {
        wipe(password);
        fail("refusing to log in: " + problem);
        return false;
    }
    if (!problem.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: %s (allowed in a dev build only)", problem.c_str());
    }
    if (username.empty() || password.empty()) {
        wipe(password);
        fail("login needs a user name and a password", "Enter a user name and a password.");
        return false;
    }
    std::string body = "username=" + formEncode(username) + "&password=";
    std::string encodedPassword = formEncode(password);
    body += encodedPassword;
    wipe(encodedPassword);
    wipe(password);

    inbound_.clear();
    outbound_.clear();
    disconnectReason_ = -1;
    wipe(activationUrl_);
    setState(State::HttpLogin, kHttpGuardMs);
    SDL_Log("session: HTTP login at %s", loggableUrl(config_.httpUrl).c_str());
    http_.login(config_.httpUrl, std::move(body));
    return true;
}

bool Session::quickPlay() {
    if (state_ != State::Idle && state_ != State::Failed) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: quick play while %s; ignored", stateName(state_));
        return false;
    }
    std::string problem;
    if (!checkLoginUrls(config_.httpUrl, config_.socketUrl, kRequireSecure, problem)) {
        fail("refusing to log in: " + problem);
        return false;
    }
    inbound_.clear();
    outbound_.clear();
    setState(State::Register, kHttpGuardMs);
    SDL_Log("session: quick play, registering an anonymous user at %s", loggableUrl(config_.httpUrl).c_str());
    http_.registerAnonymous(config_.httpUrl);
    return true;
}

bool Session::resumeStored() {
    if (state_ != State::Idle && state_ != State::Failed) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: resume while %s; ignored", stateName(state_));
        return false;
    }
    platform::StoredTokens tokens;
    if (!platform::loadTokens(tokens)) {
        SDL_Log("session: no tokens in sessionStorage; log in");
        return false;
    }
    std::string problem;
    if (!checkLoginUrls(config_.httpUrl, config_.socketUrl, kRequireSecure, problem)) {
        platform::wipe(tokens);
        fail("refusing to log in: " + problem);
        return false;
    }
    inbound_.clear();
    outbound_.clear();
    disconnectReason_ = -1;
    accessToken_ = std::move(tokens.accessToken);
    socketAccessToken_ = std::move(tokens.socketAccessToken);
    platform::wipe(tokens);
    SDL_Log("session: tokens from sessionStorage (autoLogin); choose a game server");
    setState(State::ChooseServer, 0);
    listener_.onLoggedIn();
    return true;
}

bool Session::requestForgotPassword(std::string_view email) {
    if (forgotPasswordInFlight()) {
        return false;
    }
    std::string error;
    if (!checkHttpUrl(config_.httpUrl, kRequireSecure, error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: forgot password refused: %s", error.c_str());
        return false;
    }
    std::string body = encodeForgotPasswordBody(email);
    SDL_Log("session: forgot password request at %s", loggableUrl(config_.httpUrl).c_str());
    http_.forgotPassword(config_.httpUrl, std::move(body));
    return true;
}

void Session::openActivationPage() {
    if (activationUrl_.empty()) {
        return;
    }
    // A new tab, like Unity's Application.OpenURL. Popup blockers allow it
    // only for a short time after a click or key (research/gotchas.md).
    SDL_Log("session: opening the register-success page in a new tab");
    if (!SDL_OpenURL(activationUrl_.c_str())) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: SDL_OpenURL failed: %s", SDL_GetError());
    }
}

bool Session::switchInstance(const std::string& socketUrl) {
    if (state_ != State::Ready) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: instance switch while %s; ignored", stateName(state_));
        return false;
    }
    SDL_Log("session: switching instance; WebSocket URL from the room's websocketURL");
    socket_->abandon();
    outbound_.clear();
    disconnectReason_ = -1;
    instanceUrl_ = socketUrl;
    return connectTo(socketUrl, socketAccessToken_);
}

bool Session::requestGameServers() {
    if (state_ != State::ChooseServer || accessToken_.empty() || gameServersInFlight()) {
        return false;
    }
    SDL_Log("session: listing game servers at %s", loggableUrl(config_.httpUrl).c_str());
    http_.gameServers(config_.httpUrl, accessToken_);
    return true;
}

bool Session::selectServer(std::int64_t serverId) {
    if (state_ != State::ChooseServer || accessToken_.empty() || serverId <= 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: server pick while %s; ignored", stateName(state_));
        return false;
    }
    serverId_ = serverId;
    instanceId_ = 0;
    SDL_Log("session: game server %lld picked; asking for its instances", static_cast<long long>(serverId));
    setState(State::ServerInstance, kHttpGuardMs);
    http_.serverInstances(config_.httpUrl, accessToken_, serverId);
    return true;
}

bool Session::requestApi(HttpStep step, const char* method, std::string_view path, std::string jsonBody,
                         bool firstPage) {
    if (!isGameHttpStep(step) || accessToken_.empty() || http_.inFlight(step)) {
        return false;
    }
    http_.request(step, method, config_.httpUrl + std::string(path), accessToken_, std::move(jsonBody), firstPage);
    return true;
}

bool Session::openRedirect(std::string_view targetUri) {
    if (accessToken_.empty()) {
        return false;
    }
    std::string encoded = formEncode(accessToken_);
    std::string url = config_.httpUrl + "/redirect-proxy?accessToken=" + encoded + "&targetUri=" + std::string(targetUri);
    wipe(encoded);
    SDL_Log("session: opening redirect-proxy targetUri=%.*s in a new tab", static_cast<int>(targetUri.size()),
            targetUri.data());
    const bool opened = SDL_OpenURL(url.c_str());
    if (!opened) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: SDL_OpenURL failed: %s", SDL_GetError());
    }
    wipe(url);
    return opened;
}

bool Session::connectTo(const std::string& socketUrl, const std::string& socketAccessToken) {
    std::string error;
    if (!checkSocketUrl(socketUrl, kRequireSecure, error)) {
        fail("refusing to dial the socket: " + error);
        return false;
    }
    if (!checkSocketUrl(socketUrl, true, error)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: socket URL is not encrypted (allowed in a dev build only)");
    }
    if (socketAccessToken.empty()) {
        fail("no socket access token to log in with");
        return false;
    }
    if (&socketAccessToken != &socketAccessToken_) {
        socketAccessToken_ = socketAccessToken;
    }
    outbound_.clear();
    appId_ = -1;
    userName_.assign({});
    connectedUrl_ = socketUrl;
    setState(State::Connecting, kOpenTimeoutMs);
    SDL_Log("session: dialing %s", loggableUrl(socketUrl).c_str());
    if (!socket_->open(socketUrl)) {
        fail("the socket URL was refused");
        return false;
    }
    return true;
}

bool Session::sendApp(std::string_view command, std::string_view dataJson) {
    if (state_ != State::Ready) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: app request %.*s while %s; not sent",
                    static_cast<int>(command.size()), command.data(), stateName(state_));
        return false;
    }
    const std::string frame = encodeAppRequest(appId_, command, dataJson);
    if (frame.empty()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: app request %.*s has bad JSON data; not sent",
                    static_cast<int>(command.size()), command.data());
        return false;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "session: send app request %.*s (%zu bytes)",
                 static_cast<int>(command.size()), command.data(), frame.size());
    return queue(frame);
}

bool Session::sendFrame(const std::string& frame) {
    if (state_ != State::Ready) {
        return false;
    }
    return queue(frame);
}

void Session::setPingPeriod(int periodMs) {
    if (periodMs == pingPeriodMs_) {
        return;
    }
    pingPeriodMs_ = periodMs;
    if (state_ == State::Ready) {
        socket_->startPing(pingPeriodMs_);
    }
}

bool Session::queue(const std::string& frame) {
    if (frame.size() > kMaxOutboundFrameBytes) {
        // The server would close the session with 1009 (plan 17).
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: frame of %zu bytes is over the 2048-byte limit; not sent",
                    frame.size());
        return false;
    }
    if (!outbound_.push(frame)) {
        fail("outbound queue is full");
        return false;
    }
    return true;
}

void Session::update() {
    InboundMessage message;
    // Bounded: a callback cannot push while this loop runs (one thread).
    while (state_ != State::Failed && inbound_.pop(message)) {
        handle(message);
    }
    if (state_ == State::Failed) {
        inbound_.clear();
        return;
    }
    if (inbound_.overflowed()) {
        fail("inbound queue overflowed");
        return;
    }
    if (deadlineMs_ != 0 && SDL_GetTicks() >= deadlineMs_) {
        char reason[96];
        std::snprintf(reason, sizeof reason, "%s timed out", stateName(state_));
        fail(reason, "The server is not responding. Please try again later.");
    }
}

void Session::flush() {
    while (const std::string* frame = outbound_.front()) {
        if (!socket_->send(*frame)) {
            break;
        }
        outbound_.pop();
    }
}

void Session::disconnect() {
    socket_->abandon();
    inbound_.clear();
    outbound_.clear();
    forgetTokens();
    wipe(activationUrl_);
    serverId_ = 0;
    instanceId_ = 0;
    state_ = State::Idle;
    deadlineMs_ = 0;
}

void Session::logout() {
    disconnect();
    platform::clearTokens();
}

void Session::tokenRejected(const char* why) {
    SDL_Log("session: the server rejected the token (%s); deleting it from sessionStorage", why);
    platform::clearTokens();
}

void Session::setState(State state, std::uint64_t timeoutMs) {
    state_ = state;
    deadlineMs_ = timeoutMs == 0 ? 0 : SDL_GetTicks() + timeoutMs;
}

void Session::fail(const std::string& reason, const char* message) {
    if (state_ == State::Failed) {
        return;
    }
    const State failedIn = state_;
    socket_->abandon();
    outbound_.clear();
    forgetTokens();
    serverId_ = 0;
    instanceId_ = 0;
    state_ = State::Failed;
    deadlineMs_ = 0;
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "session: failed during %s", stateName(failedIn));
    if (!message) {
        message = failedIn == State::Ready ? "Lost the connection to the server."
                                           : "Cannot connect to the server. Please try again later.";
    }
    listener_.onSessionFailed(reason, message);
}

void Session::forgetTokens() {
    wipe(accessToken_);
    wipe(socketAccessToken_);
}

void Session::handle(InboundMessage& message) {
    // ---- HTTP ----
    if (auto* registered = std::get_if<HttpRegisterOk>(&message)) {
        if (state_ != State::Register) {
            wipe(registered->password);
            return;
        }
        SDL_Log("session: registered anonymous user %s", registered->username.c_str());
        state_ = State::Idle;
        login(registered->username, registered->password); // wipes the password
        return;
    }
    if (std::holds_alternative<HttpGameServerList>(message) || isGameHttpResult(message)) {
        messages_.dispatch(message);
        return;
    }
    if (auto* ok = std::get_if<HttpLoginOk>(&message)) {
        if (state_ != State::HttpLogin) {
            wipe(ok->accessToken);
            wipe(ok->socketAccessToken);
            return;
        }
        accessToken_ = std::move(ok->accessToken);
        socketAccessToken_ = std::move(ok->socketAccessToken);
        platform::saveTokens(accessToken_, socketAccessToken_);
        SDL_Log("session: HTTP login ok; choose a game server");
        setState(State::ChooseServer, 0);
        listener_.onLoggedIn();
        return;
    }
    if (auto* notActivated = std::get_if<HttpLoginNotActivated>(&message)) {
        if (state_ != State::HttpLogin) {
            wipe(notActivated->accessToken);
            return;
        }
        // Unity: {http}/redirect-proxy?accessToken=...&targetUri=register-success.
        std::string encoded = formEncode(notActivated->accessToken);
        wipe(notActivated->accessToken);
        activationUrl_ = config_.httpUrl + "/redirect-proxy?accessToken=" + encoded + "&targetUri=register-success";
        wipe(encoded);
        SDL_Log("session: HTTP login answered: account is not activated");
        openActivationPage();
        fail("this account is not activated yet", "This account is not activated yet. Check your email.");
        return;
    }
    if (std::holds_alternative<HttpForgotPasswordOk>(message)) {
        messages_.dispatch(message);
        return;
    }
    if (const auto* failed = std::get_if<HttpFailed>(&message)) {
        if (failed->step == HttpFailed::Step::GameServers || failed->step == HttpFailed::Step::ServerInstances) {
            const bool ours = failed->step == HttpFailed::Step::GameServers ? state_ == State::ChooseServer
                                                                           : state_ == State::ServerInstance;
            if (!ours) {
                return;
            }
            if (failed->status == 401 || failed->status == 403) {
                tokenRejected(failed->step == HttpFailed::Step::GameServers ? "game servers" : "server instances");
                fail(std::string("game master refused the token: ") + failed->reason, userMessage(*failed));
                return;
            }
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: %s failed: %s",
                        failed->step == HttpFailed::Step::GameServers ? "game server list" : "server instances",
                        failed->reason.c_str());
            setState(State::ChooseServer, 0);
            messages_.dispatch(message);
            return;
        }
        if (failed->step == HttpFailed::Step::ForgotPassword || isGameHttpStep(failed->step)) {
            messages_.dispatch(message);
            return;
        }
        if (state_ != State::HttpLogin && state_ != State::Register) {
            return;
        }
        char elapsed[48];
        std::snprintf(elapsed, sizeof elapsed, " after %.0f ms", failed->elapsedMs);
        const char* step = failed->step == HttpFailed::Step::Register ? "quick play registration " : "login ";
        fail(std::string(step) + failed->reason + elapsed, userMessage(*failed));
        return;
    }
    if (const auto* instance = std::get_if<HttpServerInstance>(&message)) {
        if (state_ != State::ServerInstance) {
            return;
        }
        SDL_Log("session: game server %lld, instance %lld on host %s", static_cast<long long>(serverId_),
                static_cast<long long>(instance->instanceId), instance->host.c_str());
        instanceId_ = instance->instanceId;
        udpPort_ = config_.udpPort > 0 ? config_.udpPort : instance->udpPort;
#ifdef __EMSCRIPTEN__
        const std::string& instanceUrl = instance->instanceWebSocketUrl;
#else
        const std::string& instanceUrl = instance->instanceTcpUrl;
#endif
        instanceUrl_ = instance->instanceWebSocketUrl;
        if (!config_.socketUrl.empty()) {
            SDL_Log("session: dialing the socket URL from configs.json instead of the instance's");
            connectTo(config_.socketUrl, socketAccessToken_);
        } else if (instanceUrl.empty()) {
            fail("the game server instance has no socket URL for this build",
                 "The game server is not available. Try again later.");
        } else {
            connectTo(instanceUrl, socketAccessToken_);
        }
        return;
    }

    // ---- WebSocket events ----
    if (std::holds_alternative<SocketOpened>(message)) {
        if (state_ != State::Connecting) {
            return;
        }
        SDL_Log("session: socket open; handshake");
        setState(State::Handshake, kStepTimeoutMs);
        queue(encodeHandshake(makeClientId(), kClientType, Session::kClientVersion));
        return;
    }
    if (std::holds_alternative<SocketError>(message)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: socket error during %s", stateName(state_));
        if (state_ == State::Connecting) {
            fail("socket error before it opened");
        }
        // Once open, the close event that follows carries the details.
        return;
    }
    if (const auto* closed = std::get_if<SocketClosed>(&message)) {
        if (disconnectReason_ == kDisconnectAnotherSessionLogin) {
            fail("another session logged in as this user", "This account logged in somewhere else.");
            return;
        }
        char reason[256];
        std::snprintf(reason, sizeof reason, "WebSocket closed during %s (code %d%s%s%s)", stateName(state_),
                      closed->code, closed->clean ? ", clean" : "", closed->reason.size ? ": " : "",
                      closed->reason.c_str());
        fail(reason);
        return;
    }

    // ---- Session frames ----
    if (std::holds_alternative<HandshakeOk>(message)) {
        if (state_ != State::Handshake) {
            outOfOrder_.warn("a HANDSHAKE frame", "not waiting for one");
            return;
        }
        SDL_Log("session: handshake ok; logging in to zone %s", config_.zoneName.c_str());
        setState(State::ZoneLogin, kStepTimeoutMs);
        std::string frame = encodeZoneLogin(config_.zoneName, socketAccessToken_);
        queue(frame);
        wipe(frame);
        return;
    }
    if (const auto* login = std::get_if<ZoneLoginOk>(&message)) {
        if (state_ != State::ZoneLogin) {
            outOfOrder_.warn("a LOGIN frame", "not waiting for one");
            return;
        }
        if (login->zoneName.view() != config_.zoneName) {
            fail("server logged in to zone " + std::string(login->zoneName.view()) + ", expected " +
                 config_.zoneName);
            return;
        }
        userName_ = login->userName;
        SDL_Log("session: logged in to zone %s as %s; accessing app %s", login->zoneName.c_str(),
                login->userName.c_str(), config_.appName.c_str());
        setState(State::AppAccess, kStepTimeoutMs);
        queue(encodeAppAccess(config_.appName));
        return;
    }
    if (const auto* access = std::get_if<AppAccessOk>(&message)) {
        if (state_ != State::AppAccess) {
            outOfOrder_.warn("an APP_ACCESS frame", "not waiting for one");
            return;
        }
        if (access->appName.view() != config_.appName) {
            fail("server opened app " + std::string(access->appName.view()) + ", expected " + config_.appName);
            return;
        }
        appId_ = access->appId;
        SDL_Log("session: app %s ready (app id %d)", access->appName.c_str(), appId_);
        setState(State::Ready, 0);
        socket_->startPing(pingPeriodMs_);
#ifndef __EMSCRIPTEN__
        if (socket_->usesUdp()) {
            if (udpPort_ > 0) {
                socket_->udpConnect(udpPort_);
            } else {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION,
                            "session: udpUsage is on but there is no UDP port; `s` stays on TCP");
            }
        }
#endif
        listener_.onSessionReady();
        return;
    }
    if (const auto* error = std::get_if<SessionError>(&message)) {
        char reason[224];
        std::snprintf(reason, sizeof reason, "%s refused (code %d%s%s)", errorCommandName(error->command),
                      error->code, error->message.size ? ": " : "", error->message.c_str());
        if (state_ == State::Ready && error->command == Command::AppRequestError) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "session: %s", reason);
            // The match counts these next to its unanswered `h` (plan 09).
            messages_.dispatch(message);
            return;
        }
        if (error->command == Command::LoginError) {
            tokenRejected("zone login");
            fail(reason, "Your session has expired. Please log in again.");
            return;
        }
        fail(reason, "Internal server error. Please try again later.");
        return;
    }
    if (std::holds_alternative<Pong>(message)) {
        return;
    }
    if (const auto* disconnected = std::get_if<Disconnected>(&message)) {
        disconnectReason_ = disconnected->reason;
        SDL_Log("session: server sent DISCONNECT, reason %d", disconnected->reason);
        return;
    }

    // ---- App commands ----
    if (state_ != State::Ready) {
        outOfOrder_.warn("an app command", "the session is not ready");
        return;
    }
    messages_.dispatch(message);
}

} // namespace vibe::net

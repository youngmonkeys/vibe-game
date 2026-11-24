#pragma once

#include <concepts>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

#include "vibe/net/MessageDispatcher.h"
#include "vibe/net/Messages.h"
#include "vibe/net/Protocol.h"
#include "vibe/net/Queues.h"
#include "vibe/net/WarnLimiter.h"
#include "vibe/net/transport/Http.h"
#include "vibe/net/transport/SocketTransport.h"

namespace vibe::net {

// Where the session connects (the parts of configs.json it uses).
struct SessionConfig {
    std::string httpUrl;
    std::string masterUrl;
    std::string zoneName;
    std::string appName;
    // socket.webSocketUrl (web) or socket.tcpUrl (macOS): dialed only when the
    // picked game server's first instance has no URL for this build.
    std::string socketUrl;
    bool udpUsage = false;
    int udpPort = 0;
};

// What the session reports to its owner. Called from Session::update(), on
// the frame, never from a network callback.
class SessionListener {
public:
    virtual ~SessionListener() = default;
    // The HTTP login (or the stored tokens) worked: pick a game server with
    // requestGameServers() and selectServer().
    virtual void onLoggedIn() {}
    // App access is done; app requests may be sent.
    virtual void onSessionReady() {}
    // The login failed, or the session closed. `reason` is the detail for the
    // log: it never holds a token, the password, or a header, but may hold
    // the server's own text. `message` is the short general line to show.
    virtual void onSessionFailed(const std::string& /*reason*/, const std::string& /*message*/) {}
};

// Login and the EzyFox session (plan 06): HTTP login, the game server the
// player picks and its first instance, then the WebSocket handshake, zone login, and app access, with the frames plan 17
// recorded. Network callbacks only push into the inbound queue; update()
// drains it on the frame. Sends are queued and leave in flush(), which the
// app calls at the end of SDL_AppIterate.
//
// Plan 11: after an HTTP login the two tokens are also written to the tab's
// sessionStorage (platform/SessionStore.h), so resumeStored() can skip the
// password after a reload. logout() and a server that rejects the token delete
// them there. Nothing here logs a token, the password, a request header, or a
// whole session frame.
class Session {
public:
    enum class State {
        Idle,
        Register,
        HttpLogin,
        ChooseServer,
        ServerInstance,
        Connecting,
        Handshake,
        ZoneLogin,
        AppAccess,
        Ready,
        Failed,
    };

    // Sent in the handshake; the login screen shows it too.
    static constexpr const char* kClientVersion = "0.1.0";
    // Chrome never times out a WebSocket upgrade that gets no answer (plan 17).
    static constexpr std::uint64_t kOpenTimeoutMs = 10000;
    // Handshake, zone login, and app access each.
    static constexpr std::uint64_t kStepTimeoutMs = 10000;
    // Fetch has its own 15 s timeout; this only guards against a lost callback.
    static constexpr std::uint64_t kHttpGuardMs = HttpClient::kTimeoutMs + 5000;
    // Any period under the server's 60 s idle close works (plan 17). Plan 08
    // pings every 1 s in a match (setPingPeriod).
    static constexpr int kPingPeriodMs = 5000;
    static constexpr int kMatchPingPeriodMs = 1000;

    Session(SessionConfig config, const Protocol& protocol, SessionListener& listener);

    // Decoded app commands (JoinLobby, PlayerPose, ...) once ready, an
    // APP_REQUEST_ERROR while ready (SessionError), and the answers to
    // requestGameServers(), requestForgotPassword() and request():
    // HttpGameServerList, HttpForgotPasswordOk, a game HTTP message, or
    // HttpFailed with that step. A selectServer() that found no usable
    // instance also ends here, as HttpFailed with step ServerInstances, and
    // the session is back in ChooseServer.
    MessageDispatcher& messages() { return messages_; }

    // HTTP login, then onLoggedIn(). The password is used for the form body
    // and then wiped. Returns false (and reports onSessionFailed) when it
    // cannot start.
    bool login(std::string_view username, std::string& password);

    // Quick play: POST /ezylogin/api/v1/anonymous/register, then login() with
    // the returned user name and password.
    bool quickPlay();

    // Plan 11 (`autoLogin`): the tokens kept in sessionStorage by an earlier
    // login in this tab, then onLoggedIn(), as Unity's LoginPresenter does
    // with PlayerPrefs. Returns false, and starts nothing,
    // when none are stored. A rejected token deletes them.
    bool resumeStored();

    // Plan 11: POST forgot-password with this email. The answer arrives
    // through messages() (HttpForgotPasswordOk or HttpFailed). Needs no
    // login. Returns false when one is still running or the URL is refused.
    bool requestForgotPassword(std::string_view email);
    bool forgotPasswordInFlight() const { return http_.inFlight(HttpFailed::Step::ForgotPassword); }

    // Plan 11: the last login answered with an account that is not
    // activated. The session opened Unity's register-success page in a new
    // tab when the answer arrived; the UI can offer to open it again (a
    // popup blocker may have stopped the first one). The URL holds the
    // access token and never leaves this class.
    bool hasActivationPage() const { return !activationUrl_.empty(); }
    void openActivationPage();

    // After onLoggedIn(): the RUNNING game servers, answered through
    // messages(). Returns false when not logged in or one is running.
    bool requestGameServers();
    bool gameServersInFlight() const { return http_.inFlight(HttpFailed::Step::GameServers); }

    // After onLoggedIn(): asks for the game server's available lobby
    // instances and dials the first one (configs.json's WebSocket URL when it
    // has none). onSessionReady or onSessionFailed follows, or an HttpFailed
    // with step ServerInstances when the server has no usable instance.
    bool selectServer(std::int64_t serverId);
    // The game server picked, or 0.
    std::int64_t serverId() const { return serverId_; }
    // The lobby instance dialed for that server, or 0.
    std::int64_t instanceId() const { return instanceId_; }

    // Dials a known instance and logs in with its socket access token,
    // without asking the game master. Plan 07's room switch and plan 19's resume use
    // this directly.
    bool connectTo(const std::string& socketUrl, const std::string& socketAccessToken);

    // Plan 07's room switch: drops the current socket and dials `webSocketUrl`
    // with the socket access token kept from the login. The tokens survive;
    // onSessionReady or onSessionFailed follows. Only when Ready.
    bool switchInstance(const std::string& socketUrl);

    // An HTTP call for a game step (isGameHttpStep) with the access token
    // kept from the login. `path` is appended to the configs.json `http` URL
    // ("/api/v1/balance"). Returns false, and starts nothing, when there is no
    // token or a call of that step is still running. The answer arrives
    // through messages().
    bool requestApi(HttpStep step, const char* method, std::string_view path, std::string jsonBody = {},
                    bool firstPage = true);
    bool apiInFlight(HttpStep step) const { return http_.inFlight(step); }

    // Plan 12: opens {http}/redirect-proxy?accessToken=...&targetUri=<target>
    // in a new tab, as Unity's account and checkout buttons do. The URL holds
    // the access token, so it is built here and never logged (research/
    // server-security.md, S12). Call it while handling the click or key that
    // asked for it (popup blockers). `targetUri` is a fixed path such as
    // "account" or "checkout/shopping-cart", sent as is like Unity's. Returns
    // false without a token.
    bool openRedirect(std::string_view targetUri);

    // Queues one app request. `dataJson` is JSON text, "{}" for none.
    bool sendApp(std::string_view command, std::string_view dataJson);

    template <class Request>
    bool send(const Request& request) {
        if constexpr (requires(const Request& r, std::string& data) {
                          { r.encode(data) } -> std::convertible_to<bool>;
                      }) {
            std::string data;
            return request.encode(data) && sendApp(Request::kCommand, data);
        } else {
            return sendApp(Request::kCommand, "{}");
        }
    }

    template <class Request>
    bool request(const Request& call) {
        std::string path;
        if constexpr (requires(const Request& r) { r.path(); }) {
            path = call.path();
        } else {
            path = Request::kPath;
        }
        if (path.empty()) {
            return false;
        }
        std::string body;
        if constexpr (requires(const Request& r) { r.body(); }) {
            body = call.body();
        }
        bool firstPage = true;
        if constexpr (requires(const Request& r) { r.firstPage; }) {
            firstPage = call.firstPage;
        }
        return requestApi(Request::kStep, Request::kMethod, path, std::move(body), firstPage);
    }

    template <class Request>
    bool inFlight() const {
        return apiInFlight(Request::kStep);
    }

    // Queues one already encoded session frame (for example an app request
    // built without a JSON parse). False when not ready, over 2048 bytes, or
    // the outbound queue is full.
    bool sendFrame(const std::string& frame);

    // Keep-alive PING period on the browser timer. Takes effect at once when
    // ready, otherwise at the next app access.
    void setPingPeriod(int periodMs);

    // Text bytes and frames through the socket, and stream messages before
    // coalescing (plan 08's dev overlay).
    const TrafficCounters& traffic() const { return socket_->traffic(); }
    // Stream messages the inbound queue replaced with a newer one.
    std::size_t coalescedCount() const { return inbound_.coalescedCount(); }

    // Drains the inbound queue and checks the step timeouts.
    void update();
    // Sends the queued frames. Call at the end of SDL_AppIterate.
    void flush();
    // Closes the socket and forgets the tokens.
    void disconnect();
    // disconnect(), and deletes the tokens from sessionStorage (plan 11).
    void logout();

    State state() const { return state_; }
    static const char* stateName(State state);

    // The instance this session dialed, for a redial (plan 19).
    const std::string& connectedWebSocketUrl() const { return connectedUrl_; }
    // The in-zone user name (USER#<id>), the playerName of game commands.
    const PlayerName& userName() const { return userName_; }
    int appId() const { return appId_; }

private:
    void setState(State state, std::uint64_t timeoutMs);
    // `message` is what the player sees; nullptr picks one by the state
    // the session failed in.
    void fail(const std::string& reason, const char* message = nullptr);
    void handle(InboundMessage& message);
    bool queue(const std::string& frame);
    void forgetTokens();
    // The server refused the stored token: delete it (plan 11).
    void tokenRejected(const char* why);

    SessionConfig config_;
    SessionListener& listener_;
    MessageDispatcher messages_;
    InboundQueue inbound_;
    OutboundQueue outbound_;
    HttpClient http_;
    std::unique_ptr<SocketTransport> socket_;
    int udpPort_ = 0;

    State state_ = State::Idle;
    std::uint64_t deadlineMs_ = 0;
    std::string accessToken_;
    std::string socketAccessToken_;
    std::string activationUrl_;
    std::string connectedUrl_;
    std::int64_t serverId_ = 0;
    std::int64_t instanceId_ = 0;
    PlayerName userName_;
    int appId_ = -1;
    int disconnectReason_ = -1;
    WarnLimiter outOfOrder_;
    int pingPeriodMs_ = kPingPeriodMs;
};

} // namespace vibe::net

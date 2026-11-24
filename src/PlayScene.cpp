#include "PlayScene.h"

#include <cmath>

#include "vibe/core/Log.h"

namespace game {
namespace {

constexpr float kSpeedPx = 320.0f;
constexpr float kSizePx = 48.0f;

vibe::net::SessionConfig sessionConfig(const vibe::HostConfig& config) {
    return vibe::net::SessionConfig{config.httpUrl,   config.masterUrl, config.zoneName, config.appName,
                                    config.socketUrl, config.udpUsage,  config.udpPort};
}

vibe::input::ActionMap defaultActions() {
    using vibe::GamepadAxis;
    using vibe::GamepadButton;
    using vibe::Key;
    vibe::input::ActionMap actions;
    actions.bindKey(Action::Up, Key::W)
        .bindKey(Action::Up, Key::Up)
        .bindAxis(Action::Up, GamepadAxis::LeftY, -1.0f);
    actions.bindKey(Action::Down, Key::S)
        .bindKey(Action::Down, Key::Down)
        .bindAxis(Action::Down, GamepadAxis::LeftY, 1.0f);
    actions.bindKey(Action::Left, Key::A)
        .bindKey(Action::Left, Key::Left)
        .bindAxis(Action::Left, GamepadAxis::LeftX, -1.0f);
    actions.bindKey(Action::Right, Key::D)
        .bindKey(Action::Right, Key::Right)
        .bindAxis(Action::Right, GamepadAxis::LeftX, 1.0f);
    actions.bindKey(Action::Login, Key::L).bindButton(Action::Login, GamepadButton::South);
    actions.bindKey(Action::Pause, Key::P).bindButton(Action::Pause, GamepadButton::Start);
    return actions;
}

} // namespace

PauseScene::PauseScene(vibe::Renderer& renderer, vibe::SceneStack& scenes, const vibe::input::ActionMap& actions)
    : renderer_(renderer), scenes_(scenes), actions_(actions) {}

void PauseScene::onEvent(const vibe::InputEvent& event) {
    if (actions_.pressed(Action::Pause, event)) {
        scenes_.pop();
    }
}

void PauseScene::render(float /*alpha*/) {
    const vibe::Viewport& viewport = renderer_.viewport();
    const float width = static_cast<float>(viewport.widthPx);
    const float height = static_cast<float>(viewport.heightPx);
    renderer_.fillScreenRectAt(0.0f, 0.0f, width, height, vibe::Color{0, 0, 0, 150});
    renderer_.drawScreenText(width * 0.5f - 96.0f, height * 0.5f - 12.0f, "PAUSED", vibe::Color{255, 255, 255, 255},
                             4.0f);
    renderer_.drawScreenText(width * 0.5f - 120.0f, height * 0.5f + 32.0f, "P to resume",
                             vibe::Color{200, 210, 230, 255}, 2.0f);
}

PlayScene::PlayScene(vibe::Renderer& renderer, const vibe::HostConfig& config, vibe::SceneStack& scenes)
    : renderer_(renderer),
      scenes_(scenes),
      actions_(defaultActions()),
      session_(sessionConfig(config), protocol_, *this) {
    subscriptions_ += session_.messages().on(this, &PlayScene::onGameServers);
    subscriptions_ += session_.messages().on(this, &PlayScene::onHttpFailed);
}

void PlayScene::onEvent(const vibe::InputEvent& event) {
    if (actions_.pressed(Action::Pause, event)) {
        scenes_.push<PauseScene>(renderer_, scenes_, actions_);
        return;
    }
    if (actions_.pressed(Action::Login, event)) {
        if (session_.quickPlay()) {
            status_ = "Registering a guest account...";
        }
        return;
    }
    if (event.type == vibe::EventType::MouseButtonDown || event.type == vibe::EventType::FingerDown) {
        vibe::InputEvent local = event;
        renderer_.toCanvasCoordinates(local);
        targetX_ = local.x;
        targetY_ = local.y;
        hasTarget_ = true;
    }
}

void PlayScene::frameStart() {
    session_.update();
}

void PlayScene::fixedUpdate(float stepSeconds) {
    const vibe::Viewport& viewport = renderer_.viewport();
    if (!placed_ && !viewport.empty()) {
        x_ = previousX_ = static_cast<float>(viewport.widthPx) * 0.5f;
        y_ = previousY_ = static_cast<float>(viewport.heightPx) * 0.5f;
        placed_ = true;
    }
    previousX_ = x_;
    previousY_ = y_;
    float dx = static_cast<float>(actions_.held(Action::Right) - actions_.held(Action::Left));
    float dy = static_cast<float>(actions_.held(Action::Down) - actions_.held(Action::Up));
    if (dx != 0.0f || dy != 0.0f) {
        hasTarget_ = false;
    } else if (hasTarget_) {
        dx = targetX_ - x_;
        dy = targetY_ - y_;
        if (std::hypot(dx, dy) < kSpeedPx * stepSeconds) {
            x_ = targetX_;
            y_ = targetY_;
            hasTarget_ = false;
            return;
        }
    }
    const float length = std::hypot(dx, dy);
    if (length > 0.0f) {
        x_ += dx / length * kSpeedPx * stepSeconds;
        y_ += dy / length * kSpeedPx * stepSeconds;
    }
    if (!viewport.empty()) {
        x_ = std::fmin(std::fmax(x_, kSizePx * 0.5f), static_cast<float>(viewport.widthPx) - kSizePx * 0.5f);
        y_ = std::fmin(std::fmax(y_, kSizePx * 0.5f), static_cast<float>(viewport.heightPx) - kSizePx * 0.5f);
    }
}

void PlayScene::render(float alpha) {
    const float x = previousX_ + (x_ - previousX_) * alpha;
    const float y = previousY_ + (y_ - previousY_) * alpha;
    renderer_.fillScreenRectAt(x - kSizePx * 0.5f, y - kSizePx * 0.5f, kSizePx, kSizePx,
                               vibe::Color{255, 196, 64, 255});
    if (hasTarget_) {
        renderer_.fillScreenCircle(targetX_, targetY_, 6.0f, vibe::Color{255, 255, 255, 160});
    }
    renderer_.drawScreenText(16.0f, 16.0f, "VIBE GAME SAMPLE", vibe::Color{255, 255, 255, 255}, 3.0f);
    renderer_.drawScreenText(16.0f, 56.0f, "Move: WASD, arrows, stick, click or tap. P: pause",
                             vibe::Color{200, 210, 230, 255}, 2.0f);
    renderer_.drawScreenText(16.0f, 84.0f, status_.c_str(), vibe::Color{200, 210, 230, 255}, 2.0f);
}

void PlayScene::frameEnd() {
    session_.flush();
}

void PlayScene::onLoggedIn() {
    status_ = "Logged in; listing game servers...";
    session_.requestGameServers();
}

void PlayScene::onSessionReady() {
    status_ = "Connected as " + std::string(session_.userName().view());
}

void PlayScene::onSessionFailed(const std::string& reason, const std::string& message) {
    vibe::logWarn("sample: %s", reason.c_str());
    status_ = message + " (L to retry)";
}

void PlayScene::onGameServers(const vibe::net::HttpGameServerList& list) {
    if (list.servers.empty()) {
        status_ = "No game server is running";
        return;
    }
    status_ = "Connecting to " + list.servers.front().displayName + "...";
    session_.selectServer(list.servers.front().id);
}

void PlayScene::onHttpFailed(const vibe::net::HttpFailed& failed) {
    status_ = std::string(vibe::net::userMessage(failed)) + " (L to retry)";
}

} // namespace game

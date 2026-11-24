#include "PlayScene.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>

#include <cmath>

namespace game {
namespace {

constexpr float kSpeedPx = 320.0f;
constexpr float kSizePx = 48.0f;

vibe::net::SessionConfig sessionConfig(const vibe::HostConfig& config) {
    return vibe::net::SessionConfig{config.httpUrl,   config.masterUrl, config.zoneName, config.appName,
                                    config.socketUrl, config.udpUsage,  config.udpPort};
}

} // namespace

PlayScene::PlayScene(vibe::Renderer& renderer, const vibe::HostConfig& config)
    : renderer_(renderer), session_(sessionConfig(config), protocol_, *this) {
    subscriptions_ += session_.messages().on(this, &PlayScene::onGameServers);
    subscriptions_ += session_.messages().on(this, &PlayScene::onHttpFailed);
}

void PlayScene::onEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && !event.key.repeat && event.key.key == SDLK_L) {
        if (session_.quickPlay()) {
            status_ = "Registering a guest account...";
        }
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_FINGER_DOWN) {
        SDL_Event local = event;
        renderer_.toCanvasCoordinates(local);
        if (local.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            targetX_ = local.button.x;
            targetY_ = local.button.y;
        } else {
            targetX_ = local.tfinger.x;
            targetY_ = local.tfinger.y;
        }
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
    const bool* keys = SDL_GetKeyboardState(nullptr);
    float dx = static_cast<float>((keys[SDL_SCANCODE_D] || keys[SDL_SCANCODE_RIGHT]) -
                                  (keys[SDL_SCANCODE_A] || keys[SDL_SCANCODE_LEFT]));
    float dy = static_cast<float>((keys[SDL_SCANCODE_S] || keys[SDL_SCANCODE_DOWN]) -
                                  (keys[SDL_SCANCODE_W] || keys[SDL_SCANCODE_UP]));
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
    renderer_.drawScreenText(16.0f, 56.0f, "Move: WASD, arrows, click or tap", vibe::Color{200, 210, 230, 255}, 2.0f);
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
    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "sample: %s", reason.c_str());
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

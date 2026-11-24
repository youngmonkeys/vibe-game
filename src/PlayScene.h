#pragma once

#include <string>

#include "vibe/core/HostConfig.h"
#include "vibe/core/Input.h"
#include "vibe/core/Scene.h"
#include "vibe/net/Session.h"
#include "vibe/render/Renderer.h"

namespace game {

class PlayScene final : public vibe::Scene, private vibe::net::SessionListener {
public:
    PlayScene(vibe::Renderer& renderer, const vibe::HostConfig& config);

    void onEvent(const vibe::InputEvent& event) override;
    void frameStart() override;
    void fixedUpdate(float stepSeconds) override;
    void render(float alpha) override;
    void frameEnd() override;

private:
    void onLoggedIn() override;
    void onSessionReady() override;
    void onSessionFailed(const std::string& reason, const std::string& message) override;
    void onGameServers(const vibe::net::HttpGameServerList& list);
    void onHttpFailed(const vibe::net::HttpFailed& failed);

    vibe::Renderer& renderer_;
    vibe::net::Protocol protocol_;
    vibe::net::Session session_;
    vibe::net::Subscriptions subscriptions_;
    std::string status_ = "Press L to log in as a guest";
    float x_ = 0.0f;
    float y_ = 0.0f;
    float previousX_ = 0.0f;
    float previousY_ = 0.0f;
    float targetX_ = 0.0f;
    float targetY_ = 0.0f;
    bool hasTarget_ = false;
    bool placed_ = false;
};

} // namespace game

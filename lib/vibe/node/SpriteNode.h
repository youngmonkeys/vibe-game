#pragma once

#include <cstdint>
#include <string>
#include <utility>

#include "vibe/render/Renderer.h"
#include "vibe/render/Texture.h"
#include "vibe/node/Node.h"

namespace vibe {

class SpriteNode : public Node {
public:
    SpriteNode() = default;
    explicit SpriteNode(std::string name) : Node(std::move(name)) {}

    const Texture* texture() const { return texture_; }
    void setTexture(const Texture* texture);
    const PixelRect& source() const { return source_; }
    void setSource(const PixelRect& source) { source_ = source; }
    void setPivot(float pivotX, float pivotY);
    void setUnitsPerPixel(float unitsPerPixel) { unitsPerPixel_ = unitsPerPixel; }
    void setTint(Color tint) { tint_ = tint; }
    void setAdditive(bool additive) { additive_ = additive; }

protected:
    void onRender(Renderer& renderer, const Mat4& world) override;

private:
    const Texture* texture_ = nullptr;
    PixelRect source_;
    float pivotX_ = 0.0f;
    float pivotY_ = 0.0f;
    float unitsPerPixel_ = 0.01f;
    Color tint_{255, 255, 255, 255};
    bool additive_ = false;
};

} // namespace vibe

#include "vibe/node/SpriteNode.h"

namespace vibe {

void SpriteNode::setTexture(const Texture* texture) {
    texture_ = texture;
    if (texture_ && texture_->valid()) {
        source_ = PixelRect{0.0f, 0.0f, static_cast<float>(texture_->width()), static_cast<float>(texture_->height())};
        pivotX_ = source_.w * 0.5f;
        pivotY_ = source_.h * 0.5f;
    }
}

void SpriteNode::setPivot(float pivotX, float pivotY) {
    pivotX_ = pivotX;
    pivotY_ = pivotY;
}

void SpriteNode::onRender(Renderer& renderer, const Mat4& world) {
    if (!texture_ || !texture_->valid()) {
        return;
    }
    constexpr float kDegreesPerRadian = 57.29577951308232f;
    const Vec3 at = world.translation();
    SpriteDraw draw;
    draw.source = source_;
    draw.pivotX = pivotX_;
    draw.pivotY = pivotY_;
    draw.worldX = at.x;
    draw.worldY = at.y;
    draw.unitsPerPixelX = unitsPerPixel_ * world.planarScaleX();
    draw.unitsPerPixelY = unitsPerPixel_ * world.planarScaleY();
    draw.angleDegrees = world.rotationZ() * kDegreesPerRadian;
    draw.tintR = tint_.r;
    draw.tintG = tint_.g;
    draw.tintB = tint_.b;
    draw.alpha = tint_.a;
    draw.additive = additive_;
    renderer.drawSprite(*texture_, draw);
}

} // namespace vibe

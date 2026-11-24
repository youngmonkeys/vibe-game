#pragma once

#include <cstdint>

namespace vibe {

// A GPU texture owned by the renderer's backend. Move-only. Create it with
// Renderer::createTextureRgba or Renderer::loadPng, and destroy it (let it go
// out of scope) before the Renderer is destroyed. Gameplay code holds these
// without including SDL_render.h.
class Texture {
public:
    Texture() = default;
    ~Texture();
    Texture(const Texture&) = delete;
    Texture& operator=(const Texture&) = delete;
    Texture(Texture&& other) noexcept;
    Texture& operator=(Texture&& other) noexcept;

    bool valid() const { return texture_ != nullptr; }
    int width() const { return width_; }
    int height() const { return height_; }

private:
    friend class Renderer;
    Texture(void* texture, int width, int height) : texture_(texture), width_(width), height_(height) {}
    void reset();

    void* texture_ = nullptr;
    int width_ = 0;
    int height_ = 0;
};

// A rectangle in texture pixels, origin top left, for drawing one frame of an
// atlas (plan 05 reads these from the manifest).
struct PixelRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

// One atlas frame placed in the world (plan 05). The pivot is in texture
// pixels from the top-left of `source`, y down (manifest `pivotPx`).
// `unitsPerPixelX/Y` is 1 / pixelsPerUnit times any scale. `angleDegrees` is
// world rotation, counter-clockwise because world Y is up.
struct SpriteDraw {
    PixelRect source;
    float pivotX = 0.0f;
    float pivotY = 0.0f;
    float worldX = 0.0f;
    float worldY = 0.0f;
    float unitsPerPixelX = 0.01f;
    float unitsPerPixelY = 0.01f;
    float angleDegrees = 0.0f;
    // Multiplied into the texture color (SDL_SetTextureColorMod).
    std::uint8_t tintR = 255;
    std::uint8_t tintG = 255;
    std::uint8_t tintB = 255;
    // Multiplied into the texture alpha (SDL_SetTextureAlphaMod).
    std::uint8_t alpha = 255;
    // Added onto what is below instead of blended over it: with a white tint
    // and a low alpha, a second additive draw flashes a sprite toward white.
    bool additive = false;
};

} // namespace vibe

#pragma once

namespace vibe {

// World space matches the server: X right, Y up (docs/architecture.md,
// Coordinates and camera). The camera is orthographic with a fixed vertical
// span, so resizing the canvas changes the pixel scale, never the world span.
struct Camera {
    static constexpr float kHalfHeight = 15.0f;

    // World point at the center of the canvas.
    float x = 0.0f;
    float y = 0.0f;
};

// Canvas size in pixels, from SDL_GetWindowSizeInPixels.
struct Viewport {
    int widthPx = 0;
    int heightPx = 0;

    bool empty() const { return widthPx <= 0 || heightPx <= 0; }
};

// SDL_Renderer screen space: X right, Y down, in pixels.
struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

inline float pixelsPerUnit(const Viewport& viewport) {
    return static_cast<float>(viewport.heightPx) / (2.0f * Camera::kHalfHeight);
}

// The formula from docs/architecture.md. The only Y flip in the client.
inline ScreenPoint worldToScreen(const Camera& camera, const Viewport& viewport, float worldX, float worldY) {
    const float scale = pixelsPerUnit(viewport);
    return {(worldX - camera.x) * scale + static_cast<float>(viewport.widthPx) * 0.5f,
            static_cast<float>(viewport.heightPx) * 0.5f - (worldY - camera.y) * scale};
}

} // namespace vibe

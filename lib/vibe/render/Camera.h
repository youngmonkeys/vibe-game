#pragma once

#include "vibe/math/Transform.h"

namespace vibe {

enum class Projection { Orthographic, Perspective };

// World space matches the server: X right, Y up (docs/architecture.md,
// Coordinates and camera). The camera is orthographic with a fixed vertical
// span, so resizing the canvas changes the pixel scale, never the world span.
struct Camera {
    static constexpr float kDefaultOrthoHalfHeight = 15.0f;

    // World point at the center of the canvas.
    Vec3 position;
    Quat rotation;
    Projection projection = Projection::Orthographic;
    float orthoHalfHeight = kDefaultOrthoHalfHeight;
    float fovYRadians = 1.0471976f;
    float nearPlane = 0.1f;
    float farPlane = 1000.0f;

    static Camera at(Vec3 position) {
        Camera camera;
        camera.position = position;
        return camera;
    }

    Mat4 world() const { return Mat4::compose(position, rotation, Vec3{1.0f, 1.0f, 1.0f}); }
    Mat4 view() const { return world().affineInverse(); }
    Mat4 projectionMatrix(float aspect) const {
        if (projection == Projection::Perspective) {
            return Mat4::perspective(fovYRadians, aspect, nearPlane, farPlane);
        }
        return Mat4::orthographic(orthoHalfHeight * aspect, orthoHalfHeight, -farPlane, farPlane);
    }
    Mat4 viewProjection(float aspect) const { return projectionMatrix(aspect) * view(); }
};

// Canvas size in pixels, from SDL_GetWindowSizeInPixels.
struct Viewport {
    int widthPx = 0;
    int heightPx = 0;

    bool empty() const { return widthPx <= 0 || heightPx <= 0; }
    float aspect() const { return heightPx > 0 ? static_cast<float>(widthPx) / static_cast<float>(heightPx) : 1.0f; }
};

// SDL_Renderer screen space: X right, Y down, in pixels.
struct ScreenPoint {
    float x = 0.0f;
    float y = 0.0f;
};

inline float pixelsPerUnit(const Camera& camera, const Viewport& viewport) {
    return static_cast<float>(viewport.heightPx) / (2.0f * camera.orthoHalfHeight);
}

// The formula from docs/architecture.md. The only Y flip in the client.
inline ScreenPoint worldToScreen(const Camera& camera, const Viewport& viewport, float worldX, float worldY) {
    const float scale = pixelsPerUnit(camera, viewport);
    return {(worldX - camera.position.x) * scale + static_cast<float>(viewport.widthPx) * 0.5f,
            static_cast<float>(viewport.heightPx) * 0.5f - (worldY - camera.position.y) * scale};
}

inline bool projectToScreen(const Camera& camera, const Viewport& viewport, Vec3 world, ScreenPoint& out,
                            float* depth = nullptr) {
    const Vec4 clip = camera.viewProjection(viewport.aspect()).transform(Vec4{world.x, world.y, world.z, 1.0f});
    if (clip.w <= 0.0f) {
        return false;
    }
    const float ndcX = clip.x / clip.w;
    const float ndcY = clip.y / clip.w;
    out.x = (ndcX * 0.5f + 0.5f) * static_cast<float>(viewport.widthPx);
    out.y = (0.5f - ndcY * 0.5f) * static_cast<float>(viewport.heightPx);
    if (depth) {
        *depth = clip.z / clip.w;
    }
    return true;
}

inline void screenToWorldRay(const Camera& camera, const Viewport& viewport, float screenX, float screenY,
                             Vec3& origin, Vec3& direction) {
    const float ndcX = screenX / static_cast<float>(viewport.widthPx) * 2.0f - 1.0f;
    const float ndcY = 1.0f - screenY / static_cast<float>(viewport.heightPx) * 2.0f;
    const Mat4 world = camera.world();
    if (camera.projection == Projection::Perspective) {
        const float tanHalf = std::tan(camera.fovYRadians * 0.5f);
        const Vec3 local{ndcX * tanHalf * viewport.aspect(), ndcY * tanHalf, -1.0f};
        const float length = std::sqrt(local.x * local.x + local.y * local.y + local.z * local.z);
        origin = camera.position;
        direction = world.transformPoint(local * (1.0f / length)) - origin;
        return;
    }
    const float halfHeight = camera.orthoHalfHeight;
    origin = world.transformPoint(Vec3{ndcX * halfHeight * viewport.aspect(), ndcY * halfHeight, camera.farPlane});
    direction = world.transformPoint(Vec3{0.0f, 0.0f, -1.0f}) - camera.position;
}

} // namespace vibe

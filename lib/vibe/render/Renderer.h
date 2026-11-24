#pragma once

#include <SDL3/SDL_video.h>

#include <cstdint>

#include "vibe/render/Camera.h"
#include "vibe/render/Texture.h"

struct SDL_Renderer;
struct SDL_Surface;
union SDL_Event;

namespace vibe {

// How a texture is sampled when scaled (SDL_ScaleMode).
enum class TextureFilter {
    Linear,
    // Nearest texels with smoothed edges between them (SDL_SCALEMODE_PIXELART,
    // SDL 3.4): pixel art at a non-integer scale without blur or uneven
    // texel widths (the arena art, plan 11).
    PixelArt,
};

struct Color {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// A rectangle in canvas pixels, top-left origin, Y down (plan 11's UI).
struct ScreenRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;

    bool contains(float px, float py) const { return px >= x && py >= y && px < x + w && py < y + h; }
};

// A point in canvas pixels (plan 11's styled shapes).
struct ScreenVec {
    float x = 0.0f;
    float y = 0.0f;
};

// One dot on the minimap (plan 11), in world units.
struct MinimapDot {
    float x = 0.0f;
    float y = 0.0f;
    bool self = false;
};

// The only caller of SDL_Renderer (decision D1, docs/architecture.md). Scenes
// draw in world units through it and never see SDL_render.h. A frame is
// beginFrame, draw calls, endFrame; draw calls outside a frame do nothing.
class Renderer {
public:
    // Call after SDL_Init and before SDL_CreateWindow, and add the returned
    // flags to the window. Asks for an OpenGL ES 3.0 context, which Emscripten
    // creates as WebGL 2, the only version this build links. Without it SDL's
    // GLES2 renderer asks for ES 2.0 (WebGL 1) and Emscripten logs an error.
    static SDL_WindowFlags prepareWindow();

    // Creates the renderer with vsync on, which SDL's Emscripten backend turns
    // into requestAnimationFrame timing. Vsync off would switch the main loop
    // to setTimeout. Logs SDL_GetError() and returns false on failure.
    bool create(SDL_Window* window);
    void destroy();

    // Reads the canvas size and clears it. Returns false, and draws nothing,
    // when the canvas has no pixels; then skip endFrame so nothing is presented.
    bool beginFrame(Color clear);
    // Presents the frame started by a successful beginFrame.
    void endFrame();

#ifdef VIBE_DEV
    // For the dev overlay's ImGui SDL_Renderer backend only (plan 07,
    // src/app/debug/). Nothing else draws around this class.
    SDL_Renderer* sdlRenderer() const { return renderer_; }
#endif

    // The page's one window, for SDL text input (plan 11's text fields).
    SDL_Window* window() const { return window_; }
    // Mouse and touch events carry CSS pixels; drawing uses canvas pixels.
    // Converts the event's coordinates in place (SDL_ConvertEventToRender-
    // Coordinates). Plan 11's widgets hit-test in canvas pixels.
    void toCanvasCoordinates(SDL_Event& event) const;

    void setCamera(const Camera& camera) { camera_ = camera; }
    const Camera& camera() const { return camera_; }
    // Canvas size read by the last beginFrame.
    const Viewport& viewport() const { return viewport_; }

    // Axis-aligned rectangle with world corners (x0, y0) and (x1, y1).
    void fillWorldRect(float x0, float y0, float x1, float y1, Color color);
    // Rectangle outline whose edges are centered on the world rectangle and
    // are `thicknessPx` canvas pixels wide at any zoom.
    void strokeWorldRect(float x0, float y0, float x1, float y1, float thicknessPx, Color color);
    // Draws `source` (the whole texture when null) as a `width` by `height`
    // world-unit quad centered on (centerX, centerY), upright.
    void drawSprite(const Texture& texture, const PixelRect* source, float centerX, float centerY, float width,
                    float height);
    // Draws one atlas frame with its pivot on a world point, rotated about
    // that pivot (plan 05). See SpriteDraw.
    void drawSprite(const Texture& texture, const SpriteDraw& sprite);
    // Rectangle in canvas pixels, top-left at (x, y). For the dev overlay.
    void fillScreenRectAt(float x, float y, float w, float h, Color color);
    // Plan 11's UI, all in canvas pixels.
    void fillScreenRect(const ScreenRect& rect, Color color) { fillScreenRectAt(rect.x, rect.y, rect.w, rect.h, color); }
    // Outline inside `rect`, `thicknessPx` wide.
    void strokeScreenRect(const ScreenRect& rect, float thicknessPx, Color color);
    void fillScreenCircle(float centerX, float centerY, float radius, Color color);
    // The whole texture at its own pixel size, top-left at (x, y), multiplied
    // by `tint` (text is rendered white and tinted here).
    void drawScreenTexture(const Texture& texture, float x, float y, Color tint);
    // The whole texture stretched into `rect`, tinted.
    void drawScreenTexture(const Texture& texture, const ScreenRect& rect, Color tint);
    // A nine-slice frame (SDL_RenderTexture9Grid): the texture's `corner`
    // pixel border keeps its shape, scaled by `scale`, and the edges and
    // middle stretch to fill `rect`. The UI's pixel-art panels and buttons.
    void drawScreenNineSlice(const Texture& texture, const ScreenRect& rect, float corner, float scale, Color tint);
    // The whole texture at its pixel size with a vertical color gradient
    // multiplied in (gold text).
    void drawScreenTextureGradient(const Texture& texture, float x, float y, Color top, Color bottom);
    // As above, stretched into `rect` (the pixel font's scaled text).
    void drawScreenTextureGradient(const Texture& texture, const ScreenRect& rect, Color top, Color bottom);

    // ---- Styled shapes (plan 11), all in canvas pixels, allocation free ----
    // A convex polygon (at most kMaxShapePoints, either winding), filled with
    // a vertical gradient from `top` at `gradientTop` to `bottom` at
    // `gradientTop + gradientHeight`.
    static constexpr int kMaxShapePoints = 16;
    void fillConvex(const ScreenVec* points, int count, float gradientTop, float gradientHeight, Color top,
                    Color bottom);
    // The polygon's outline, `thicknessPx` wide, inside its edges.
    void strokeConvex(const ScreenVec* points, int count, float thicknessPx, Color color);
    // A soft ellipse: `inner` at the center fading to `outer` at the edge.
    void fillGlow(float centerX, float centerY, float radiusX, float radiusY, Color inner, Color outer);
    // A rectangle with a vertical gradient.
    void fillGradientRect(const ScreenRect& rect, Color top, Color bottom);

    // Draw calls only inside `rect` until clearScreenClip().
    void setScreenClip(const ScreenRect& rect);
    void clearScreenClip();
    // Plan 11's minimap: a second orthographic view of the whole map
    // [0, mapSize] x [0, mapSize] into `area` (decision D1: no render
    // target), with the playable border and one dot per player, green for
    // self and red for others, drawn after the others so it stays on top.
    // `ground`, when given, is drawn as the map instead of a flat fill (the
    // arena art's minimap image, covering [0, mapSize] on both axes).
    void drawMinimap(const ScreenRect& area, float mapSize, float border, const MinimapDot* dots, int count,
                     const Texture* ground = nullptr);

    // ---- World-space images (plan 11's arena art) ----
    // The whole texture stretched over the world rectangle with lower-left
    // corner (left, bottom) and the given size, upright.
    void drawWorldTexture(const Texture& texture, float left, float bottom, float width, float height);
    // Repeats the texture over the whole canvas, one copy per `tileUnits`
    // world units, aligned so a copy starts at every multiple of `tileUnits`
    // on both axes; it moves with the camera.
    void fillViewportTiled(const Texture& texture, float tileUnits);
    // Debug circle in world units: a translucent fill and a `thicknessPx`
    // outline. Use a color alpha below 255 for the fill to show through.
    void drawWorldCircle(float centerX, float centerY, float radius, Color fill, Color outline, float thicknessPx);
    // SDL's 8x8 debug font in canvas pixels, top-left at (x, y), each glyph
    // drawn `scale` times as large. For the dev overlay only.
    void drawScreenText(float x, float y, const char* text, Color color, float scale = 1.0f);

    // Sends the draw calls queued so far to WebGL (SDL_FlushRenderer), so a
    // caller can time its own submission. endFrame presents what is left.
    void flush();

    // `pixels` is width * height RGBA bytes, rows top to bottom. Nearest
    // sampling when `nearest`, linear otherwise. Returns an invalid Texture
    // and logs on failure.
    Texture createTextureRgba(int width, int height, const std::uint8_t* pixels, bool nearest);
    // As loadPng, but every pixel becomes white with its own alpha, so one
    // icon can be tinted any color (plan 11; the icons are black or white).
    Texture loadPngMask(const char* path);
    Texture loadPngMask(const char* path, TextureFilter filter);
    // As loadPng, with the given sampling.
    Texture loadPng(const char* path, TextureFilter filter);
    // Decodes a PNG through SDL_LoadPNG_IO (SDL's bundled stb_image, see
    // research/dependencies.md) from `path` under the asset root, with
    // SDL_IOFromFile. Linear sampling. Returns an invalid Texture and logs on
    // failure.
    Texture loadPng(const char* path);

private:
    friend class TextCache;
    void fillScreenRect(float x, float y, float w, float h);
    // Uploads `surface` (not freed). Invalid on failure.
    Texture createTextureFromSurface(SDL_Surface* surface, TextureFilter filter = TextureFilter::Linear);

    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    Camera camera_;
    Viewport viewport_;
    bool inFrame_ = false;
    // Set while the canvas has no pixels, so the skip is logged once.
    bool skippingEmpty_ = false;
};

} // namespace vibe

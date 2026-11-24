#include "vibe/render/Renderer.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_render.h>
#include <SDL3/SDL_surface.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace vibe {
namespace {

// Draw calls run every frame. Log the first failure only, so a lost context
// does not flood the console.
void logDrawError(const char* what) {
    static bool logged = false;
    if (!logged) {
        logged = true;
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "%s failed: %s (further draw errors are not logged)", what,
                     SDL_GetError());
    }
}

} // namespace

Texture::~Texture() { reset(); }

Texture::Texture(Texture&& other) noexcept
    : texture_(other.texture_), width_(other.width_), height_(other.height_) {
    other.texture_ = nullptr;
    other.width_ = 0;
    other.height_ = 0;
}

Texture& Texture::operator=(Texture&& other) noexcept {
    if (this != &other) {
        reset();
        texture_ = other.texture_;
        width_ = other.width_;
        height_ = other.height_;
        other.texture_ = nullptr;
        other.width_ = 0;
        other.height_ = 0;
    }
    return *this;
}

void Texture::reset() {
    if (texture_) {
        SDL_DestroyTexture(texture_);
        texture_ = nullptr;
    }
    width_ = 0;
    height_ = 0;
}

SDL_WindowFlags Renderer::prepareWindow() {
#ifdef __EMSCRIPTEN__
    if (!SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3) ||
        !SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_GL_SetAttribute failed: %s", SDL_GetError());
    }
    return SDL_WINDOW_OPENGL;
#else
    return 0;
#endif
}

bool Renderer::create(SDL_Window* window) {
    const SDL_PropertiesID props = SDL_CreateProperties();
    if (!props) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateProperties failed: %s", SDL_GetError());
        return false;
    }
    SDL_SetPointerProperty(props, SDL_PROP_RENDERER_CREATE_WINDOW_POINTER, window);
    SDL_SetNumberProperty(props, SDL_PROP_RENDERER_CREATE_PRESENT_VSYNC_NUMBER, 1);
    renderer_ = SDL_CreateRendererWithProperties(props);
    SDL_DestroyProperties(props);
    if (!renderer_) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateRenderer failed: %s", SDL_GetError());
        return false;
    }
    // Alpha blending for draw-color shapes too (debug circles). Shapes with
    // alpha 255 draw as before.
    if (!SDL_SetRenderDrawBlendMode(renderer_, SDL_BLENDMODE_BLEND)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetRenderDrawBlendMode failed: %s", SDL_GetError());
    }
    window_ = window;
    SDL_Log("renderer: %s", SDL_GetRendererName(renderer_));
    return true;
}

void Renderer::destroy() {
    if (renderer_) {
        SDL_DestroyRenderer(renderer_);
        renderer_ = nullptr;
    }
    window_ = nullptr;
    inFrame_ = false;
}

bool Renderer::beginFrame(Color clear) {
    inFrame_ = false;
    if (!renderer_) {
        return false;
    }
    if (!SDL_GetWindowSizeInPixels(window_, &viewport_.widthPx, &viewport_.heightPx)) {
        logDrawError("SDL_GetWindowSizeInPixels");
        return false;
    }
    if (viewport_.empty()) {
        if (!skippingEmpty_) {
            skippingEmpty_ = true;
            SDL_Log("render: canvas has no pixels (%dx%d), not drawing", viewport_.widthPx, viewport_.heightPx);
        }
        return false;
    }
    if (skippingEmpty_) {
        skippingEmpty_ = false;
        SDL_Log("render: canvas has pixels again (%dx%d), drawing", viewport_.widthPx, viewport_.heightPx);
    }
    if (!SDL_SetRenderDrawColor(renderer_, clear.r, clear.g, clear.b, clear.a) || !SDL_RenderClear(renderer_)) {
        logDrawError("clear");
        return false;
    }
    inFrame_ = true;
    return true;
}

void Renderer::endFrame() {
    if (!inFrame_) {
        return;
    }
    inFrame_ = false;
    if (!SDL_RenderPresent(renderer_)) {
        logDrawError("SDL_RenderPresent");
    }
}

void Renderer::fillScreenRect(float x, float y, float w, float h) {
    const SDL_FRect rect{x, y, w, h};
    if (!SDL_RenderFillRect(renderer_, &rect)) {
        logDrawError("SDL_RenderFillRect");
    }
}

void Renderer::fillWorldRect(float x0, float y0, float x1, float y1, Color color) {
    if (!inFrame_) {
        return;
    }
    const ScreenPoint a = worldToScreen(camera_, viewport_, std::min(x0, x1), std::max(y0, y1));
    const ScreenPoint b = worldToScreen(camera_, viewport_, std::max(x0, x1), std::min(y0, y1));
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    fillScreenRect(a.x, a.y, b.x - a.x, b.y - a.y);
}

void Renderer::strokeWorldRect(float x0, float y0, float x1, float y1, float thicknessPx, Color color) {
    if (!inFrame_) {
        return;
    }
    // a is the top-left corner on screen, b the bottom-right.
    const ScreenPoint a = worldToScreen(camera_, viewport_, std::min(x0, x1), std::max(y0, y1));
    const ScreenPoint b = worldToScreen(camera_, viewport_, std::max(x0, x1), std::min(y0, y1));
    const float t = std::max(thicknessPx, 1.0f);
    const float h = t * 0.5f;
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    fillScreenRect(a.x - h, a.y - h, b.x - a.x + t, t); // top
    fillScreenRect(a.x - h, b.y - h, b.x - a.x + t, t); // bottom
    fillScreenRect(a.x - h, a.y + h, t, b.y - a.y - t); // left
    fillScreenRect(b.x - h, a.y + h, t, b.y - a.y - t); // right
}

void Renderer::drawSprite(const Texture& texture, const PixelRect* source, float centerX, float centerY, float width,
                          float height) {
    if (!inFrame_ || !texture.valid()) {
        return;
    }
    const float scale = pixelsPerUnit(viewport_);
    const ScreenPoint center = worldToScreen(camera_, viewport_, centerX, centerY);
    const SDL_FRect dst{center.x - width * scale * 0.5f, center.y - height * scale * 0.5f, width * scale,
                        height * scale};
    SDL_FRect src;
    if (source) {
        src = SDL_FRect{source->x, source->y, source->w, source->h};
    }
    if (!SDL_RenderTexture(renderer_, texture.texture_, source ? &src : nullptr, &dst)) {
        logDrawError("SDL_RenderTexture");
    }
}

void Renderer::drawSprite(const Texture& texture, const SpriteDraw& sprite) {
    if (!inFrame_ || !texture.valid()) {
        return;
    }
    const float scale = pixelsPerUnit(viewport_);
    const float kx = sprite.unitsPerPixelX * scale;
    const float ky = sprite.unitsPerPixelY * scale;
    const ScreenPoint pivot = worldToScreen(camera_, viewport_, sprite.worldX, sprite.worldY);
    const SDL_FRect src{sprite.source.x, sprite.source.y, sprite.source.w, sprite.source.h};
    const SDL_FRect dst{pivot.x - sprite.pivotX * kx, pivot.y - sprite.pivotY * ky, sprite.source.w * kx,
                        sprite.source.h * ky};
    const SDL_FPoint center{sprite.pivotX * kx, sprite.pivotY * ky};
    const bool tinted = sprite.tintR != 255 || sprite.tintG != 255 || sprite.tintB != 255;
    if (tinted) {
        SDL_SetTextureColorMod(texture.texture_, sprite.tintR, sprite.tintG, sprite.tintB);
    }
    if (sprite.alpha != 255) {
        SDL_SetTextureAlphaMod(texture.texture_, sprite.alpha);
    }
    if (sprite.additive) {
        SDL_SetTextureBlendMode(texture.texture_, SDL_BLENDMODE_ADD);
    }
    // World angles are counter-clockwise (Y up); SDL's are clockwise (Y down).
    if (!SDL_RenderTextureRotated(renderer_, texture.texture_, &src, &dst, -static_cast<double>(sprite.angleDegrees),
                                  &center, SDL_FLIP_NONE)) {
        logDrawError("SDL_RenderTextureRotated");
    }
    if (tinted) {
        SDL_SetTextureColorMod(texture.texture_, 255, 255, 255);
    }
    if (sprite.alpha != 255) {
        SDL_SetTextureAlphaMod(texture.texture_, 255);
    }
    if (sprite.additive) {
        SDL_SetTextureBlendMode(texture.texture_, SDL_BLENDMODE_BLEND);
    }
}

void Renderer::drawWorldCircle(float centerX, float centerY, float radius, Color fill, Color outline,
                               float thicknessPx) {
    if (!inFrame_) {
        return;
    }
    constexpr int kSegments = 48;
    const float scale = pixelsPerUnit(viewport_);
    const ScreenPoint c = worldToScreen(camera_, viewport_, centerX, centerY);
    const float r = radius * scale;
    const float half = std::max(thicknessPx, 1.0f) * 0.5f;
    const SDL_FColor fillColor{fill.r / 255.0f, fill.g / 255.0f, fill.b / 255.0f, fill.a / 255.0f};
    const SDL_FColor lineColor{outline.r / 255.0f, outline.g / 255.0f, outline.b / 255.0f, outline.a / 255.0f};
    // A fan for the fill and a strip of quads for the outline. Fixed arrays,
    // so drawing allocates nothing.
    std::array<SDL_Vertex, kSegments * 3> fan{};
    std::array<SDL_Vertex, kSegments * 6> ring{};
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = static_cast<float>(i) * (6.28318531f / kSegments);
        const float a1 = static_cast<float>(i + 1) * (6.28318531f / kSegments);
        const float c0 = std::cos(a0), s0 = std::sin(a0), c1 = std::cos(a1), s1 = std::sin(a1);
        SDL_Vertex* f = &fan[static_cast<std::size_t>(i * 3)];
        f[0] = {{c.x, c.y}, fillColor, {0, 0}};
        f[1] = {{c.x + c0 * r, c.y + s0 * r}, fillColor, {0, 0}};
        f[2] = {{c.x + c1 * r, c.y + s1 * r}, fillColor, {0, 0}};
        const SDL_FPoint in0{c.x + c0 * (r - half), c.y + s0 * (r - half)};
        const SDL_FPoint out0{c.x + c0 * (r + half), c.y + s0 * (r + half)};
        const SDL_FPoint in1{c.x + c1 * (r - half), c.y + s1 * (r - half)};
        const SDL_FPoint out1{c.x + c1 * (r + half), c.y + s1 * (r + half)};
        SDL_Vertex* q = &ring[static_cast<std::size_t>(i * 6)];
        q[0] = {in0, lineColor, {0, 0}};
        q[1] = {out0, lineColor, {0, 0}};
        q[2] = {out1, lineColor, {0, 0}};
        q[3] = {in0, lineColor, {0, 0}};
        q[4] = {out1, lineColor, {0, 0}};
        q[5] = {in1, lineColor, {0, 0}};
    }
    if (fill.a > 0 && !SDL_RenderGeometry(renderer_, nullptr, fan.data(), kSegments * 3, nullptr, 0)) {
        logDrawError("SDL_RenderGeometry");
    }
    if (outline.a > 0 && !SDL_RenderGeometry(renderer_, nullptr, ring.data(), kSegments * 6, nullptr, 0)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::fillScreenRectAt(float x, float y, float w, float h, Color color) {
    if (!inFrame_) {
        return;
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    fillScreenRect(x, y, w, h);
}

void Renderer::drawScreenText(float x, float y, const char* text, Color color, float scale) {
    if (!inFrame_) {
        return;
    }
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    SDL_SetRenderScale(renderer_, scale, scale);
    if (!SDL_RenderDebugText(renderer_, x / scale, y / scale, text)) {
        logDrawError("SDL_RenderDebugText");
    }
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
}

void Renderer::toCanvasCoordinates(SDL_Event& event) const {
    if (renderer_ && !SDL_ConvertEventToRenderCoordinates(renderer_, &event)) {
        logDrawError("SDL_ConvertEventToRenderCoordinates");
    }
}

void Renderer::strokeScreenRect(const ScreenRect& rect, float thicknessPx, Color color) {
    if (!inFrame_) {
        return;
    }
    const float t = std::max(thicknessPx, 1.0f);
    SDL_SetRenderDrawColor(renderer_, color.r, color.g, color.b, color.a);
    fillScreenRect(rect.x, rect.y, rect.w, t);
    fillScreenRect(rect.x, rect.y + rect.h - t, rect.w, t);
    fillScreenRect(rect.x, rect.y + t, t, rect.h - 2.0f * t);
    fillScreenRect(rect.x + rect.w - t, rect.y + t, t, rect.h - 2.0f * t);
}

void Renderer::fillScreenCircle(float centerX, float centerY, float radius, Color color) {
    if (!inFrame_ || radius <= 0.0f) {
        return;
    }
    constexpr int kSegments = 20;
    const SDL_FColor c{color.r / 255.0f, color.g / 255.0f, color.b / 255.0f, color.a / 255.0f};
    std::array<SDL_Vertex, kSegments * 3> fan{};
    for (int i = 0; i < kSegments; ++i) {
        const float a0 = static_cast<float>(i) * (6.28318531f / kSegments);
        const float a1 = static_cast<float>(i + 1) * (6.28318531f / kSegments);
        SDL_Vertex* f = &fan[static_cast<std::size_t>(i * 3)];
        f[0] = {{centerX, centerY}, c, {0, 0}};
        f[1] = {{centerX + std::cos(a0) * radius, centerY + std::sin(a0) * radius}, c, {0, 0}};
        f[2] = {{centerX + std::cos(a1) * radius, centerY + std::sin(a1) * radius}, c, {0, 0}};
    }
    if (!SDL_RenderGeometry(renderer_, nullptr, fan.data(), kSegments * 3, nullptr, 0)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::drawScreenTexture(const Texture& texture, float x, float y, Color tint) {
    if (!inFrame_ || !texture.valid()) {
        return;
    }
    const SDL_FRect dst{x, y, static_cast<float>(texture.width()), static_cast<float>(texture.height())};
    SDL_SetTextureColorMod(texture.texture_, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture.texture_, tint.a);
    if (!SDL_RenderTexture(renderer_, texture.texture_, nullptr, &dst)) {
        logDrawError("SDL_RenderTexture");
    }
}

void Renderer::drawScreenTexture(const Texture& texture, const ScreenRect& rect, Color tint) {
    if (!inFrame_ || !texture.valid()) {
        return;
    }
    const SDL_FRect dst{rect.x, rect.y, rect.w, rect.h};
    SDL_SetTextureColorMod(texture.texture_, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture.texture_, tint.a);
    if (!SDL_RenderTexture(renderer_, texture.texture_, nullptr, &dst)) {
        logDrawError("SDL_RenderTexture");
    }
}

void Renderer::drawScreenNineSlice(const Texture& texture, const ScreenRect& rect, float corner, float scale,
                                   Color tint) {
    if (!inFrame_ || !texture.valid() || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    // Corners never larger than half the rectangle.
    scale = std::min({scale, rect.w * 0.5f / corner, rect.h * 0.5f / corner});
    const SDL_FRect dst{rect.x, rect.y, rect.w, rect.h};
    SDL_SetTextureColorMod(texture.texture_, tint.r, tint.g, tint.b);
    SDL_SetTextureAlphaMod(texture.texture_, tint.a);
    if (!SDL_RenderTexture9Grid(renderer_, texture.texture_, nullptr, corner, corner, corner, corner, scale, &dst)) {
        logDrawError("SDL_RenderTexture9Grid");
    }
}

namespace {

SDL_FColor toFColor(Color c) {
    return SDL_FColor{c.r / 255.0f, c.g / 255.0f, c.b / 255.0f, c.a / 255.0f};
}

SDL_FColor mix(Color a, Color b, float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return SDL_FColor{(a.r + (b.r - a.r) * t) / 255.0f, (a.g + (b.g - a.g) * t) / 255.0f,
                      (a.b + (b.b - a.b) * t) / 255.0f, (a.a + (b.a - a.a) * t) / 255.0f};
}

} // namespace

void Renderer::drawScreenTextureGradient(const Texture& texture, float x, float y, Color top, Color bottom) {
    drawScreenTextureGradient(
        texture, ScreenRect{x, y, static_cast<float>(texture.width()), static_cast<float>(texture.height())}, top, bottom);
}

void Renderer::drawScreenTextureGradient(const Texture& texture, const ScreenRect& rect, Color top, Color bottom) {
    if (!inFrame_ || !texture.valid()) {
        return;
    }
    const float x = rect.x;
    const float y = rect.y;
    const float w = rect.w;
    const float h = rect.h;
    const SDL_FColor t = toFColor(top);
    const SDL_FColor b = toFColor(bottom);
    const SDL_Vertex v[4] = {{{x, y}, t, {0, 0}}, {{x + w, y}, t, {1, 0}}, {{x + w, y + h}, b, {1, 1}},
                             {{x, y + h}, b, {0, 1}}};
    const int indices[6] = {0, 1, 2, 0, 2, 3};
    SDL_SetTextureColorMod(texture.texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.texture_, 255);
    if (!SDL_RenderGeometry(renderer_, texture.texture_, v, 4, indices, 6)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::fillConvex(const ScreenVec* points, int count, float gradientTop, float gradientHeight, Color top,
                          Color bottom) {
    if (!inFrame_ || count < 3 || count > kMaxShapePoints) {
        return;
    }
    ScreenVec c{};
    for (int i = 0; i < count; ++i) {
        c.x += points[i].x;
        c.y += points[i].y;
    }
    c.x /= static_cast<float>(count);
    c.y /= static_cast<float>(count);
    const float inv = gradientHeight > 0.0f ? 1.0f / gradientHeight : 0.0f;
    std::array<SDL_Vertex, kMaxShapePoints + 1> v{};
    std::array<int, kMaxShapePoints * 3> idx{};
    v[0] = {{c.x, c.y}, mix(top, bottom, (c.y - gradientTop) * inv), {0, 0}};
    for (int i = 0; i < count; ++i) {
        v[static_cast<std::size_t>(i + 1)] = {
            {points[i].x, points[i].y}, mix(top, bottom, (points[i].y - gradientTop) * inv), {0, 0}};
        idx[static_cast<std::size_t>(i * 3)] = 0;
        idx[static_cast<std::size_t>(i * 3 + 1)] = i + 1;
        idx[static_cast<std::size_t>(i * 3 + 2)] = (i + 1) % count + 1;
    }
    if (!SDL_RenderGeometry(renderer_, nullptr, v.data(), count + 1, idx.data(), count * 3)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::strokeConvex(const ScreenVec* points, int count, float thicknessPx, Color color) {
    if (!inFrame_ || count < 3 || count > kMaxShapePoints) {
        return;
    }
    // Signed area gives the winding, so the inward normal is known.
    float area = 0.0f;
    for (int i = 0; i < count; ++i) {
        const ScreenVec& a = points[i];
        const ScreenVec& b = points[(i + 1) % count];
        area += a.x * b.y - b.x * a.y;
    }
    const float side = area > 0.0f ? 1.0f : -1.0f;
    const float t = std::max(thicknessPx, 1.0f);
    std::array<ScreenVec, kMaxShapePoints> inner{};
    for (int i = 0; i < count; ++i) {
        const ScreenVec& p0 = points[(i + count - 1) % count];
        const ScreenVec& p1 = points[i];
        const ScreenVec& p2 = points[(i + 1) % count];
        const auto normal = [side](const ScreenVec& a, const ScreenVec& b) {
            const float dx = b.x - a.x;
            const float dy = b.y - a.y;
            const float len = std::max(std::sqrt(dx * dx + dy * dy), 1e-4f);
            return ScreenVec{-dy / len * side, dx / len * side};
        };
        const ScreenVec n1 = normal(p0, p1);
        const ScreenVec n2 = normal(p1, p2);
        ScreenVec m{n1.x + n2.x, n1.y + n2.y};
        const float mlen = std::max(std::sqrt(m.x * m.x + m.y * m.y), 1e-4f);
        m = {m.x / mlen, m.y / mlen};
        const float k = t / std::max(m.x * n1.x + m.y * n1.y, 0.25f);
        inner[static_cast<std::size_t>(i)] = {p1.x + m.x * k, p1.y + m.y * k};
    }
    const SDL_FColor c = toFColor(color);
    std::array<SDL_Vertex, kMaxShapePoints * 2> v{};
    std::array<int, kMaxShapePoints * 6> idx{};
    for (int i = 0; i < count; ++i) {
        const std::size_t j = static_cast<std::size_t>(i);
        v[j * 2] = {{points[i].x, points[i].y}, c, {0, 0}};
        v[j * 2 + 1] = {{inner[j].x, inner[j].y}, c, {0, 0}};
        const int a = i * 2;
        const int b = ((i + 1) % count) * 2;
        const int q[6] = {a, a + 1, b, b, a + 1, b + 1};
        for (int k = 0; k < 6; ++k) {
            idx[j * 6 + static_cast<std::size_t>(k)] = q[k];
        }
    }
    if (!SDL_RenderGeometry(renderer_, nullptr, v.data(), count * 2, idx.data(), count * 6)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::fillGlow(float centerX, float centerY, float radiusX, float radiusY, Color inner, Color outer) {
    if (!inFrame_ || radiusX <= 0.0f || radiusY <= 0.0f) {
        return;
    }
    constexpr int kSegments = 32;
    std::array<SDL_Vertex, kSegments + 1> v{};
    std::array<int, kSegments * 3> idx{};
    v[0] = {{centerX, centerY}, toFColor(inner), {0, 0}};
    const SDL_FColor edge = toFColor(outer);
    for (int i = 0; i < kSegments; ++i) {
        const float a = static_cast<float>(i) * (6.28318531f / kSegments);
        v[static_cast<std::size_t>(i + 1)] = {{centerX + std::cos(a) * radiusX, centerY + std::sin(a) * radiusY}, edge,
                                              {0, 0}};
        idx[static_cast<std::size_t>(i * 3)] = 0;
        idx[static_cast<std::size_t>(i * 3 + 1)] = i + 1;
        idx[static_cast<std::size_t>(i * 3 + 2)] = (i + 1) % kSegments + 1;
    }
    if (!SDL_RenderGeometry(renderer_, nullptr, v.data(), kSegments + 1, idx.data(), kSegments * 3)) {
        logDrawError("SDL_RenderGeometry");
    }
}

void Renderer::fillGradientRect(const ScreenRect& rect, Color top, Color bottom) {
    const ScreenVec quad[4] = {{rect.x, rect.y}, {rect.x + rect.w, rect.y}, {rect.x + rect.w, rect.y + rect.h},
                               {rect.x, rect.y + rect.h}};
    fillConvex(quad, 4, rect.y, rect.h, top, bottom);
}

void Renderer::setScreenClip(const ScreenRect& rect) {
    if (!inFrame_) {
        return;
    }
    const SDL_Rect clip{static_cast<int>(std::floor(rect.x)), static_cast<int>(std::floor(rect.y)),
                        static_cast<int>(std::ceil(rect.w)), static_cast<int>(std::ceil(rect.h))};
    if (!SDL_SetRenderClipRect(renderer_, &clip)) {
        logDrawError("SDL_SetRenderClipRect");
    }
}

void Renderer::clearScreenClip() {
    if (renderer_ && !SDL_SetRenderClipRect(renderer_, nullptr)) {
        logDrawError("SDL_SetRenderClipRect");
    }
}

void Renderer::drawWorldTexture(const Texture& texture, float left, float bottom, float width, float height) {
    if (!inFrame_ || !texture.valid() || width <= 0.0f || height <= 0.0f) {
        return;
    }
    const ScreenPoint a = worldToScreen(camera_, viewport_, left, bottom + height);
    const ScreenPoint b = worldToScreen(camera_, viewport_, left + width, bottom);
    // Skip it when it is entirely off the canvas.
    if (b.x < 0.0f || b.y < 0.0f || a.x > static_cast<float>(viewport_.widthPx) ||
        a.y > static_cast<float>(viewport_.heightPx)) {
        return;
    }
    const SDL_FRect dst{a.x, a.y, b.x - a.x, b.y - a.y};
    SDL_SetTextureColorMod(texture.texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.texture_, 255);
    if (!SDL_RenderTexture(renderer_, texture.texture_, nullptr, &dst)) {
        logDrawError("SDL_RenderTexture");
    }
}

void Renderer::fillViewportTiled(const Texture& texture, float tileUnits) {
    if (!inFrame_ || !texture.valid() || tileUnits <= 0.0f) {
        return;
    }
    const float ppu = pixelsPerUnit(viewport_);
    const float halfW = static_cast<float>(viewport_.widthPx) * 0.5f / ppu;
    const float halfH = static_cast<float>(viewport_.heightPx) * 0.5f / ppu;
    // The tile corner at or beyond the canvas's top-left, in world units.
    const float left = std::floor((camera_.x - halfW) / tileUnits) * tileUnits;
    const float top = std::ceil((camera_.y + halfH) / tileUnits) * tileUnits;
    const ScreenPoint corner = worldToScreen(camera_, viewport_, left, top);
    const float tilePx = tileUnits * ppu;
    const SDL_FRect dst{corner.x, corner.y, static_cast<float>(viewport_.widthPx) - corner.x + tilePx,
                        static_cast<float>(viewport_.heightPx) - corner.y + tilePx};
    SDL_SetTextureColorMod(texture.texture_, 255, 255, 255);
    SDL_SetTextureAlphaMod(texture.texture_, 255);
    if (!SDL_RenderTextureTiled(renderer_, texture.texture_, nullptr, tilePx / static_cast<float>(texture.width()),
                                &dst)) {
        logDrawError("SDL_RenderTextureTiled");
    }
}

void Renderer::drawMinimap(const ScreenRect& area, float mapSize, float border, const MinimapDot* dots, int count,
                           const Texture* ground) {
    if (!inFrame_ || area.w <= 0.0f || area.h <= 0.0f || mapSize <= 0.0f) {
        return;
    }
    // Orthographic: the whole map fits the square inside `area`, X right and
    // world Y up, so the one Y flip is here as in worldToScreen.
    const float side = std::min(area.w, area.h);
    const float scale = side / mapSize;
    const float left = area.x + (area.w - side) * 0.5f;
    const float top = area.y + (area.h - side) * 0.5f;
    const auto toScreen = [&](float wx, float wy) {
        return SDL_FPoint{left + wx * scale, top + (mapSize - wy) * scale};
    };
    if (ground && ground->valid()) {
        const SDL_FRect dst{left, top, side, side};
        SDL_SetTextureColorMod(ground->texture_, 255, 255, 255);
        SDL_SetTextureAlphaMod(ground->texture_, 255);
        if (!SDL_RenderTexture(renderer_, ground->texture_, nullptr, &dst)) {
            logDrawError("SDL_RenderTexture");
        }
    } else {
        SDL_SetRenderDrawColor(renderer_, 16, 24, 20, 210);
        fillScreenRect(left, top, side, side);
        SDL_SetRenderDrawColor(renderer_, 58, 94, 64, 230);
        const SDL_FPoint inner0 = toScreen(border, mapSize - border);
        const float innerSide = (mapSize - 2.0f * border) * scale;
        fillScreenRect(inner0.x, inner0.y, innerSide, innerSide);
    }
    strokeScreenRect(ScreenRect{left, top, side, side}, std::max(1.0f, side / 120.0f), Color{232, 206, 120, 255});
    const float radius = std::max(2.5f, side / 45.0f);
    for (const bool self : {false, true}) {
        for (int i = 0; i < count; ++i) {
            if (dots[i].self != self) {
                continue;
            }
            const SDL_FPoint p = toScreen(std::clamp(dots[i].x, 0.0f, mapSize), std::clamp(dots[i].y, 0.0f, mapSize));
            fillScreenCircle(p.x, p.y, radius + 1.0f, Color{0, 0, 0, 200});
            fillScreenCircle(p.x, p.y, radius, self ? Color{70, 230, 90, 255} : Color{240, 60, 50, 255});
        }
    }
}

Texture Renderer::createTextureFromSurface(SDL_Surface* surface, TextureFilter filter) {
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        return {};
    }
    if (!SDL_SetTextureScaleMode(texture,
                                 filter == TextureFilter::PixelArt ? SDL_SCALEMODE_PIXELART : SDL_SCALEMODE_LINEAR)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetTextureScaleMode failed: %s", SDL_GetError());
    }
    return Texture(texture, surface->w, surface->h);
}

void Renderer::flush() {
    if (inFrame_ && !SDL_FlushRenderer(renderer_)) {
        logDrawError("SDL_FlushRenderer");
    }
}

Texture Renderer::createTextureRgba(int width, int height, const std::uint8_t* pixels, bool nearest) {
    // SDL_CreateSurfaceFrom does not copy or write the pixels; the texture
    // upload reads them before the surface is destroyed.
    SDL_Surface* surface = SDL_CreateSurfaceFrom(width, height, SDL_PIXELFORMAT_RGBA32,
                                                 const_cast<std::uint8_t*>(pixels), width * 4);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateSurfaceFrom failed: %s", SDL_GetError());
        return {};
    }
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateTextureFromSurface failed: %s", SDL_GetError());
        return {};
    }
    if (!SDL_SetTextureScaleMode(texture, nearest ? SDL_SCALEMODE_NEAREST : SDL_SCALEMODE_LINEAR)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetTextureScaleMode failed: %s", SDL_GetError());
    }
    return Texture(texture, width, height);
}

Texture Renderer::loadPngMask(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot open %s: %s", path, SDL_GetError());
        return {};
    }
    SDL_Surface* loaded = SDL_LoadPNG_IO(io, /*closeio=*/true);
    if (!loaded) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot decode %s: %s", path, SDL_GetError());
        return {};
    }
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_ConvertSurface(%s) failed: %s", path, SDL_GetError());
        return {};
    }
    // Load time only, never on the frame.
    for (int y = 0; y < rgba->h; ++y) {
        auto* row = static_cast<std::uint8_t*>(rgba->pixels) + static_cast<std::ptrdiff_t>(y) * rgba->pitch;
        for (int x = 0; x < rgba->w; ++x) {
            row[x * 4] = row[x * 4 + 1] = row[x * 4 + 2] = 255;
        }
    }
    Texture texture = createTextureFromSurface(rgba);
    SDL_DestroySurface(rgba);
    return texture;
}

Texture Renderer::loadPng(const char* path, TextureFilter filter) {
    Texture texture = loadPng(path);
    if (texture.valid() && filter == TextureFilter::PixelArt &&
        !SDL_SetTextureScaleMode(texture.texture_, SDL_SCALEMODE_PIXELART)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetTextureScaleMode(%s, PIXELART) failed: %s", path,
                    SDL_GetError());
    }
    return texture;
}

Texture Renderer::loadPngMask(const char* path, TextureFilter filter) {
    Texture texture = loadPngMask(path);
    if (texture.valid() && filter == TextureFilter::PixelArt &&
        !SDL_SetTextureScaleMode(texture.texture_, SDL_SCALEMODE_PIXELART)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetTextureScaleMode(%s, PIXELART) failed: %s", path,
                    SDL_GetError());
    }
    return texture;
}

Texture Renderer::loadPng(const char* path) {
    SDL_IOStream* io = SDL_IOFromFile(path, "rb");
    if (!io) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot open %s: %s", path, SDL_GetError());
        return {};
    }
    SDL_Surface* surface = SDL_LoadPNG_IO(io, /*closeio=*/true);
    if (!surface) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "cannot decode %s: %s", path, SDL_GetError());
        return {};
    }
    const int width = surface->w;
    const int height = surface->h;
    SDL_Texture* texture = SDL_CreateTextureFromSurface(renderer_, surface);
    SDL_DestroySurface(surface);
    if (!texture) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_CreateTextureFromSurface(%s) failed: %s", path,
                     SDL_GetError());
        return {};
    }
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "SDL_SetTextureScaleMode failed: %s", SDL_GetError());
    }
    return Texture(texture, width, height);
}

} // namespace vibe

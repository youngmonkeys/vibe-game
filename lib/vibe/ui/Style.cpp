#include "vibe/ui/Style.h"

#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <cmath>

#include "vibe/ui/Ui.h"

namespace vibe::ui::style {
namespace {

Color withAlpha(Color c, std::uint8_t a) {
    c.a = a;
    return c;
}

Color scaled(Color c, float k) {
    const auto f = [k](std::uint8_t v) { return static_cast<std::uint8_t>(std::clamp(v * k, 0.0f, 255.0f)); };
    return Color{f(c.r), f(c.g), f(c.b), c.a};
}

// A horizontal gold rule fading out at both ends, `width` wide.
void rule(Ui& ui, float cx, float y, float width) {
    Renderer& r = ui.renderer();
    const float h = std::max(1.0f, ui.u(1.5f));
    const float half = width * 0.5f;
    // fillConvex grades top to bottom only, so the fade toward both ends is
    // three segments of falling alpha on each side.
    for (int i = 0; i < 3; ++i) {
        const float a0 = static_cast<float>(i) / 3.0f;
        const float a1 = static_cast<float>(i + 1) / 3.0f;
        const auto alpha = static_cast<std::uint8_t>(230 - i * 75);
        const ScreenVec l[4] = {{cx - half * a1, y}, {cx - half * a0, y}, {cx - half * a0, y + h}, {cx - half * a1, y + h}};
        const ScreenVec rr[4] = {{cx + half * a0, y}, {cx + half * a1, y}, {cx + half * a1, y + h}, {cx + half * a0, y + h}};
        r.fillConvex(l, 4, y, h, withAlpha(palette::kGold, alpha), withAlpha(palette::kGold, alpha));
        r.fillConvex(rr, 4, y, h, withAlpha(palette::kGold, alpha), withAlpha(palette::kGold, alpha));
    }
}

} // namespace

void chamfer(const ScreenRect& rect, float cut, ScreenVec* out) {
    cut = std::min({cut, rect.w * 0.5f, rect.h * 0.5f});
    const float x0 = rect.x, y0 = rect.y, x1 = rect.x + rect.w, y1 = rect.y + rect.h;
    out[0] = {x0 + cut, y0};
    out[1] = {x1 - cut, y0};
    out[2] = {x1, y0 + cut};
    out[3] = {x1, y1 - cut};
    out[4] = {x1 - cut, y1};
    out[5] = {x0 + cut, y1};
    out[6] = {x0, y1 - cut};
    out[7] = {x0, y0 + cut};
}

void pointed(const ScreenRect& rect, float tip, ScreenVec* out) {
    tip = std::min(tip, rect.w * 0.25f);
    const float cy = rect.y + rect.h * 0.5f;
    out[0] = {rect.x + tip, rect.y};
    out[1] = {rect.x + rect.w - tip, rect.y};
    out[2] = {rect.x + rect.w, cy};
    out[3] = {rect.x + rect.w - tip, rect.y + rect.h};
    out[4] = {rect.x + tip, rect.y + rect.h};
    out[5] = {rect.x, cy};
}

void background(Ui& ui) {
    Renderer& r = ui.renderer();
    const float w = ui.width();
    const float h = ui.height();
    r.fillGradientRect(ScreenRect{0, 0, w, h}, palette::kBackTop, palette::kBackBottom);
    // Warm light from above the title, and a dark vignette at the corners.
    r.fillGlow(w * 0.5f, h * 0.18f, w * 0.62f, h * 0.55f, Color{96, 52, 46, 150}, palette::kClear);
    r.fillGlow(w * 0.5f, h * 0.55f, w * 0.45f, h * 0.45f, Color{40, 30, 60, 90}, palette::kClear);
    // Gold motes drifting up, from fixed seeds so nothing allocates.
    const float t = static_cast<float>(SDL_GetTicks()) / 1000.0f;
    for (int i = 0; i < 26; ++i) {
        const float seed = static_cast<float>(i) * 12.9898f;
        const float fx = std::fmod(std::sin(seed) * 43758.5453f, 1.0f);
        const float fy = std::fmod(std::sin(seed * 1.7f) * 24634.6345f, 1.0f);
        const float speed = 0.012f + 0.02f * std::fabs(std::fmod(std::sin(seed * 3.1f) * 9183.1f, 1.0f));
        const float x = std::fabs(fx) * w + std::sin(t * 0.4f + seed) * ui.u(10.0f);
        float y = std::fmod(std::fabs(fy) - t * speed, 1.0f);
        if (y < 0.0f) {
            y += 1.0f;
        }
        y *= h;
        const float size = ui.u(2.0f + 3.0f * std::fabs(std::fmod(seed, 1.0f)));
        const auto alpha = static_cast<std::uint8_t>(70 + 60 * (0.5f + 0.5f * std::sin(t * 1.3f + seed)));
        r.fillGlow(x, y, size * 3.0f, size * 3.0f, Color{255, 196, 110, static_cast<std::uint8_t>(alpha / 3)},
                   palette::kClear);
        r.fillGlow(x, y, size, size, Color{255, 226, 160, alpha}, palette::kClear);
    }
}

void panel(Ui& ui, const ScreenRect& rect) {
    // A soft shadow under the pixel-art frame.
    ui.renderer().fillGlow(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.58f, rect.w * 0.62f, rect.h * 0.62f,
                           Color{0, 0, 0, 140}, palette::kClear);
    ui.frame(Frame::Panel, rect);
}

void panelHeader(Ui& ui, const ScreenRect& panel, std::string_view text, float height) {
    Renderer& r = ui.renderer();
    // A band inside the frame's border, with a pixel rule under it: a gold
    // line over a dark one, one art pixel each.
    const float px = std::max(1.0f, ui.u(kPixel));
    // Clear of the frame's corner rivets (art pixels 5 and 6 from each edge).
    const float inset = px * 4.0f;
    const float side = px * 8.0f;
    const ScreenRect strip{panel.x + side, panel.y + inset, panel.w - 2 * side, ui.u(height) - inset + px * 3.0f};
    r.fillScreenRect(strip, Color{30, 36, 68, 255});
    r.fillScreenRect(ScreenRect{strip.x, strip.y, strip.w, px}, Color{44, 52, 92, 255});
    r.fillScreenRect(ScreenRect{strip.x, strip.y + strip.h - px * 2.0f, strip.w, px}, palette::kGold);
    r.fillScreenRect(ScreenRect{strip.x, strip.y + strip.h - px, strip.w, px}, Color{46, 26, 8, 255});
    const float size = height * 0.52f;
    TextCache& tc = ui.textCache();
    const int fpx = ui.fontPx(size);
    const float textH = static_cast<float>(tc.lineHeight(FontFace::Title, fpx));
    const TextSize ts = tc.measure(FontFace::Title, fpx, text);
    const float x = std::round(strip.x + (strip.w - static_cast<float>(ts.w)) * 0.5f);
    const float y = std::round(strip.y + (strip.h - px * 2.0f - textH) * 0.5f);
    tc.draw(FontFace::Title, fpx, text, x + ui.u(2.0f), y + ui.u(2.0f), Color{0, 0, 0, 170});
    tc.drawGradient(FontFace::Title, fpx, text, x, y, palette::kGoldLight, palette::kGoldDark);
}

void card(Ui& ui, const ScreenRect& rect, bool highlighted) {
    if (highlighted) {
        ui.renderer().fillGlow(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f, rect.w * 0.75f, rect.h * 0.62f,
                               Color{255, 190, 80, 70}, palette::kClear);
    }
    ui.frame(highlighted ? Frame::CardGold : Frame::Card, rect);
}

void diamond(Ui& ui, float cx, float cy, float radius, Color color, bool glow) {
    Renderer& r = ui.renderer();
    if (glow) {
        r.fillGlow(cx, cy, radius * 3.2f, radius * 2.2f, withAlpha(color, 110), palette::kClear);
    }
    const ScreenVec outer[4] = {{cx, cy - radius}, {cx + radius, cy}, {cx, cy + radius}, {cx - radius, cy}};
    r.fillConvex(outer, 4, cy - radius, radius * 2.0f, color, scaled(color, 0.6f));
    r.strokeConvex(outer, 4, std::max(1.0f, radius * 0.18f), Color{30, 22, 10, 200});
    const float k = radius * 0.42f;
    const ScreenVec shine[4] = {{cx, cy - k}, {cx + k, cy}, {cx, cy + k}, {cx - k, cy}};
    r.fillConvex(shine, 4, cy - k, k * 2.0f, Color{255, 255, 255, 200}, withAlpha(color, 120));
}

float title(Ui& ui, std::string_view text, float cx, float y, float sizeUnits, Color top, Color bottom) {
    TextCache& tc = ui.textCache();
    const int px = ui.fontPx(sizeUnits);
    const TextSize ts = tc.measure(FontFace::Title, px, text);
    const float width = std::max(static_cast<float>(ts.w), ui.u(240.0f));
    // Top rule with a glowing diamond in the middle.
    rule(ui, cx, y, width * 0.75f);
    diamond(ui, cx, y, ui.u(6.0f), palette::kGoldLight, true);
    const float ty = y + ui.u(8.0f);
    const float x = std::round(cx - static_cast<float>(ts.w) * 0.5f);
    // A dark outline (four offsets) and a drop shadow, then the gradient.
    const float o = std::max(1.0f, ui.u(2.0f));
    const Color edge{24, 12, 4, 220};
    tc.draw(FontFace::Title, px, text, x - o, ty, edge);
    tc.draw(FontFace::Title, px, text, x + o, ty, edge);
    tc.draw(FontFace::Title, px, text, x, ty - o, edge);
    tc.draw(FontFace::Title, px, text, x, ty + o, edge);
    tc.draw(FontFace::Title, px, text, x + o * 2.0f, ty + o * 2.5f, Color{0, 0, 0, 150});
    tc.drawGradient(FontFace::Title, px, text, x, ty, top, bottom);
    const float below = ty + static_cast<float>(ts.h) + ui.u(4.0f);
    rule(ui, cx, below, width * 0.55f);
    const float d = ui.u(5.0f);
    const ScreenVec hollow[4] = {{cx, below - d}, {cx + d, below}, {cx, below + d}, {cx - d, below}};
    ui.renderer().fillConvex(hollow, 4, below - d, d * 2, palette::kBackBottom, palette::kBackBottom);
    ui.renderer().strokeConvex(hollow, 4, std::max(1.0f, ui.u(1.5f)), palette::kGold);
    return below + ui.u(10.0f);
}

void pill(Ui& ui, const ScreenRect& rect, std::string_view text, bool gold, float sizeUnits) {
    // A small pixel card: gold-brown with gold text for a highlight, navy
    // with cream text otherwise.
    ui.frame(gold ? Frame::CardGold : Frame::Card, rect);
    const float textH = static_cast<float>(ui.textCache().lineHeight(FontFace::Body, ui.fontPx(sizeUnits)));
    ui.text(text, rect.x + rect.w * 0.5f, rect.y + (rect.h - textH) * 0.5f,
            {.size = sizeUnits, .color = gold ? palette::kGoldLight : palette::kCream, .align = Align::Center,
             .maxWidth = rect.w - ui.u(16.0f)});
}

void banner(Ui& ui, const ScreenRect& rect, std::string_view text, Color color) {
    ui.frame(Frame::Card, rect);
    if (text.empty()) {
        return;
    }
    // One line when it fits; else two wrapped lines a size smaller; shrink
    // only if even that is too tall.
    TextCache& tc = ui.textCache();
    const float room = rect.w - rect.h * 2.2f;
    const float size = 20.0f;
    const float textH = static_cast<float>(tc.lineHeight(FontFace::Body, ui.fontPx(size)));
    if (static_cast<float>(tc.measure(FontFace::Body, ui.fontPx(size), text).w) > room) {
        const int px = ui.fontPx(16.0f);
        const int wrap = std::max(1, static_cast<int>(room));
        const TextSize block = tc.measure(FontFace::Body, px, text, wrap);
        if (static_cast<float>(block.h) <= rect.h - ui.u(6.0f)) {
            tc.draw(FontFace::Body, px, text, std::round(rect.x + (rect.w - static_cast<float>(block.w)) * 0.5f),
                    std::round(rect.y + (rect.h - static_cast<float>(block.h)) * 0.5f), color, wrap);
            return;
        }
    }
    ui.text(text, rect.x + rect.w * 0.5f, rect.y + (rect.h - textH) * 0.5f,
            {.size = size, .color = color, .align = Align::Center, .maxWidth = room});
}

void buttonFace(Ui& ui, const ScreenRect& rect, ButtonStyle style, bool hover, bool pressed, bool enabled) {
    Renderer& r = ui.renderer();
    if (style == ButtonStyle::Tile) {
        const float lift = enabled && hover && !pressed ? ui.u(3.0f) : 0.0f;
        const ScreenRect face{rect.x, rect.y - lift, rect.w, rect.h};
        if (enabled && hover) {
            r.fillGlow(face.x + face.w * 0.5f, face.y + face.h * 0.55f, face.w * 0.8f, face.h * 0.8f,
                       Color{255, 190, 80, 80}, palette::kClear);
        }
        card(ui, face, enabled && hover);
        return;
    }
    if (style == ButtonStyle::Link) {
        return;
    }
    if (style == ButtonStyle::Gold || style == ButtonStyle::Blue || style == ButtonStyle::Dark) {
        Frame face = Frame::ButtonDisabled;
        if (enabled) {
            const int base = style == ButtonStyle::Gold ? static_cast<int>(Frame::ButtonGold)
                             : style == ButtonStyle::Blue ? static_cast<int>(Frame::ButtonBlue)
                                                          : static_cast<int>(Frame::ButtonDark);
            face = static_cast<Frame>(base + (pressed ? 2 : hover ? 1 : 0));
        }
        ui.frame(face, rect);
        return;
    }
    // Round: the pixel-art disc.
    const Frame face = !enabled ? Frame::RoundDisabled : pressed ? Frame::RoundPressed : hover ? Frame::RoundHover : Frame::Round;
    ui.frame(face, roundSprite(rect));
}

ScreenRect pixelFit(const ScreenRect& rect, float artW, float artH) {
    const float fit = std::min(rect.w / artW, rect.h / artH);
    const float whole = std::max(1.0f, std::round(fit));
    const float p = std::fabs(whole - fit) <= fit * 0.15f ? whole : fit;
    const float w = artW * p;
    const float h = artH * p;
    return ScreenRect{std::round(rect.x + (rect.w - w) * 0.5f), std::round(rect.y + (rect.h - h) * 0.5f), w, h};
}

ScreenRect roundSprite(const ScreenRect& rect) {
    return pixelFit(rect, kRoundArtWidth, kRoundArtHeight);
}

void avatar(Ui& ui, float cx, float cy, float radius, bool gold) {
    const ScreenRect art = pixelFit(ScreenRect{cx - radius, cy - radius, radius * 2.0f, radius * 2.0f}, kAvatarArt,
                                    kAvatarArt);
    ui.icon(gold ? Icon::AvatarGold : Icon::AvatarSilver, art, Color{255, 255, 255, 255});
    // The glyph centered on the ring's pixel grid.
    const float p = art.w / kAvatarArt;
    const float inset = (kAvatarArt - kRoundGlyph) * 0.5f * p;
    ui.icon(Icon::User, ScreenRect{art.x + inset, art.y + inset, kRoundGlyph * p, kRoundGlyph * p},
            gold ? palette::kGoldLight : palette::kSilverLight);
}

// ---- Lobby polish (2026-10-22) ------------------------------------------------

float now() {
    return static_cast<float>(SDL_GetTicks()) / 1000.0f;
}

float easeOut(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float u = 1.0f - t;
    return 1.0f - u * u * u;
}

float easeOutBack(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    constexpr float c1 = 1.70158f;
    constexpr float c3 = c1 + 1.0f;
    const float u = t - 1.0f;
    return 1.0f + c3 * u * u * u + c1 * u * u;
}

void tooltip(Ui& ui, float cx, float y, std::string_view text) {
    TextCache& tc = ui.textCache();
    const int px = ui.fontPx(13.0f);
    const TextSize ts = tc.measure(FontFace::Body, px, text);
    const float pad = ui.u(12.0f);
    const float w = static_cast<float>(ts.w) + pad * 2.0f;
    const float h = static_cast<float>(ts.h) + ui.u(12.0f);
    float x = std::clamp(cx - w * 0.5f, ui.left() + ui.u(4.0f), std::max(ui.left() + ui.u(4.0f), ui.right() - w - ui.u(4.0f)));
    ui.frame(Frame::Card, ScreenRect{x, y, w, h});
    tc.draw(FontFace::Body, px, text, std::round(x + pad), std::round(y + ui.u(6.0f)), palette::kCream);
}

void toggleSwitch(Ui& ui, const ScreenRect& rect, float t) {
    Renderer& r = ui.renderer();
    t = std::clamp(t, 0.0f, 1.0f);
    const float rad = rect.h * 0.5f;
    const Color off{40, 44, 66, 255};
    const Color on{214, 156, 52, 255};
    const auto mix = [t](std::uint8_t a, std::uint8_t b) {
        return static_cast<std::uint8_t>(static_cast<float>(a) + (static_cast<float>(b) - static_cast<float>(a)) * t);
    };
    const Color track{mix(off.r, on.r), mix(off.g, on.g), mix(off.b, on.b), 255};
    r.fillScreenCircle(rect.x + rad, rect.y + rad, rad, track);
    r.fillScreenCircle(rect.x + rect.w - rad, rect.y + rad, rad, track);
    r.fillScreenRect(ScreenRect{rect.x + rad, rect.y, rect.w - 2.0f * rad, rect.h}, track);
    const float kx = rect.x + rad + (rect.w - 2.0f * rad) * t;
    r.fillGlow(kx, rect.y + rad * 1.2f, rad * 1.1f, rad * 1.1f, Color{0, 0, 0, 120}, palette::kClear);
    r.fillScreenCircle(kx, rect.y + rad, rad * 0.78f, palette::kCream);
}

void shimmer(Ui& ui, const ScreenRect& rect, float period, float offset) {
    const float phase = std::fmod(now() + offset, period) / period;
    // Sweep during the first 35% of the period, rest after.
    const float k = phase / 0.35f;
    if (k >= 1.0f) {
        return;
    }
    Renderer& r = ui.renderer();
    const float band = rect.h * 0.7f;
    const float x = rect.x - band * 2.0f + (rect.w + band * 3.0f) * easeOut(k);
    // Inside the pointed ends (buttonFace's tips are 0.32 of the height).
    const float tip = rect.h * 0.34f;
    r.setScreenClip(ScreenRect{rect.x + tip, rect.y + 1.0f, rect.w - 2.0f * tip, rect.h - 2.0f});
    const ScreenVec q[4] = {{x, rect.y}, {x + band, rect.y}, {x + band - rect.h * 0.5f, rect.y + rect.h},
                            {x - rect.h * 0.5f, rect.y + rect.h}};
    r.fillConvex(q, 4, rect.y, rect.h, Color{255, 255, 240, 110}, Color{255, 255, 240, 30});
    r.clearScreenClip();
}

void levelBadge(Ui& ui, float cx, float cy, float radius, std::string_view level) {
    const ScreenRect art = pixelFit(ScreenRect{cx - radius * 1.25f, cy - radius * 1.35f, radius * 2.5f, radius * 2.7f},
                                    kLevelArtWidth, kLevelArtHeight);
    ui.icon(Icon::Level, art, Color{255, 255, 255, 255});
    // Shrinks to fit inside the bevel for long numbers.
    const float units = radius / ui.scale() * 1.3f;
    const float textH = static_cast<float>(ui.textCache().lineHeight(FontFace::Title, ui.fontPx(units)));
    const float p = art.w / kLevelArtWidth;
    ui.text(level, art.x + art.w * 0.5f, std::round(art.y + (art.h - textH) * 0.5f),
            {.face = FontFace::Title, .size = units, .color = Color{70, 40, 8, 255}, .align = Align::Center,
             .maxWidth = art.w - p * 5.0f});
}

void progressBar(Ui& ui, const ScreenRect& rect, float fraction, Color top, Color bottom) {
    Renderer& r = ui.renderer();
    r.fillScreenRect(rect, Color{6, 8, 16, 220});
    fraction = std::clamp(fraction, 0.0f, 1.0f);
    if (fraction <= 0.0f) {
        return;
    }
    const ScreenRect fill{rect.x, rect.y, std::max(rect.h, rect.w * fraction), rect.h};
    r.fillGradientRect(fill, top, bottom);
    // A glint running along the filled part.
    const float g = std::fmod(now() * 0.6f, 1.0f);
    r.setScreenClip(fill);
    r.fillGlow(fill.x + fill.w * g, fill.y + fill.h * 0.5f, rect.h * 3.0f, rect.h, Color{255, 255, 255, 120},
               palette::kClear);
    r.clearScreenClip();
}

void twinkle(Ui& ui, float cx, float cy, float radius, Color color) {
    Renderer& r = ui.renderer();
    const float k = radius * 0.22f;
    const ScreenVec v[4] = {{cx, cy - radius}, {cx + k, cy}, {cx, cy + radius}, {cx - k, cy}};
    const ScreenVec h[4] = {{cx - radius, cy}, {cx, cy - k}, {cx + radius, cy}, {cx, cy + k}};
    r.fillConvex(v, 4, cy - radius, radius * 2.0f, color, color);
    r.fillConvex(h, 4, cy - k, k * 2.0f, color, color);
    r.fillGlow(cx, cy, radius * 0.9f, radius * 0.9f, withAlpha(color, 90), palette::kClear);
}

} // namespace vibe::ui::style

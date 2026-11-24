#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "vibe/render/Renderer.h"
#include "vibe/render/Texture.h"

namespace vibe {

// The two UI typefaces (plan 11). Both are the Unity project's fonts from
// the manifest. Both draw only capitals; Pingsan's "1" reads like a "7", so
// numbers and body text use Boohong (research/assets-and-presentation.md).
enum class FontFace : std::uint8_t { Body, Title };

// Width and height of a rendered string in canvas pixels.
struct TextSize {
    int w = 0;
    int h = 0;
};

// Text drawn with SDL3_ttf, cached as one texture per (face, pixel size,
// string). A label is rendered once and re-rendered only when its text or
// size changes; drawing a cached label allocates nothing. Entries unused for
// a while are freed in endFrame(), so a changing number does not grow the
// cache. Part of src/render/ (decision D1): callers never see SDL_ttf.
class TextCache {
public:
    // Unused this many frames: freed.
    static constexpr std::uint64_t kEvictAfterFrames = 180;
    // Kept even when used every frame; past this the least recently used go.
    static constexpr std::size_t kMaxEntries = 512;

    explicit TextCache(Renderer& renderer);
    ~TextCache();
    TextCache(const TextCache&) = delete;
    TextCache& operator=(const TextCache&) = delete;

    // TTF_Init, then both fonts. A face that fails logs and falls back to the
    // other. Returns false only when neither loads; text then draws nothing.
    //
    // `pixelGrid` above 0: both faces are pixel fonts drawn on that grid
    // (EzysPixel: 14 px, tools/gen_ui_font.py). Text is then rendered once at
    // the grid size, where every font pixel is a whole pixel, and drawn scaled
    // as pixel art: a size of N px draws the grid at N / kPixelSizePerGrid
    // times. `fallbackPath`, when given, supplies characters the faces lack.
    bool init(const char* bodyPath, const char* titlePath, int pixelGrid = 0, const char* fallbackPath = nullptr);
    // With a pixel font, a requested size of this many px draws one grid
    // pixel as one canvas pixel (capitals 7/11 of the size).
    static constexpr float kPixelSizePerGrid = 11.0f;
    void shutdown();
    bool ready() const;

    // Size of `text` at `sizePx`, rendering it into the cache if needed.
    // `wrapPx` above 0 wraps at that width (SDL_ttf breaks at spaces), left
    // aligned; 0 is one line.
    TextSize measure(FontFace face, int sizePx, std::string_view text, int wrapPx = 0);
    // Draws `text` with its top-left at (x, y), tinted `color`.
    TextSize draw(FontFace face, int sizePx, std::string_view text, float x, float y, Color color, int wrapPx = 0);
    // As draw(), with a vertical gradient from `top` to `bottom` (gold
    // titles).
    TextSize drawGradient(FontFace face, int sizePx, std::string_view text, float x, float y, Color top,
                          Color bottom);
    // Plan 12: how many bytes of `text` fit in `maxWidthPx` on one line
    // (TTF_MeasureString), never splitting a UTF-8 character. Renders
    // nothing, so a long chat line can be clipped before it becomes a texture.
    std::size_t fitBytes(FontFace face, int sizePx, std::string_view text, int maxWidthPx);
    // Height of one line at this size (TTF_GetFontHeight).
    int lineHeight(FontFace face, int sizePx);

    // Once per frame after drawing: frees stale entries.
    void endFrame();
    std::size_t size() const { return entries_.size(); }
    // Textures rendered since the start, for the dev overlay and page tests.
    std::uint64_t renders() const { return renders_; }

private:
    struct Entry {
        FontFace face = FontFace::Body;
        int sizePx = 0;
        int wrapPx = 0;
        std::string text;
        Texture texture;
        TextSize size;
        std::uint64_t lastUsed = 0;
    };
    struct Fonts;
    void* fontFor(FontFace face, int sizePx);
    // Canvas pixels per rendered pixel: 1, or the pixel font's scale.
    float scaleFor(int sizePx) const;
    const Entry* find(FontFace face, int sizePx, std::string_view text, int wrapPx);

    Renderer& renderer_;
    std::unique_ptr<Fonts> fonts_;
    int pixelGrid_ = 0;
    bool ttfStarted_ = false;
    std::vector<Entry> entries_;
    std::uint64_t frame_ = 1;
    std::uint64_t renders_ = 0;
    bool warnedRender_ = false;
};

} // namespace vibe

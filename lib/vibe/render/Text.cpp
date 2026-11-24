#include "vibe/render/Text.h"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_surface.h>
#include <SDL3_ttf/SDL_ttf.h>

#include <algorithm>
#include <cmath>

namespace vibe {
namespace {

// Sizes outside this range are clamped: a tiny canvas still gets legible
// text, and a huge one does not make huge textures.
constexpr int kMinSizePx = 8;
constexpr int kMaxSizePx = 160;

TTF_Font* openFont(const char* path, const char* role, float sizePx = 24.0f) {
    if (!path || !*path) {
        return nullptr;
    }
    TTF_Font* font = TTF_OpenFont(path, sizePx);
    if (!font) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "text: cannot load the %s font %s: %s", role, path,
                     SDL_GetError());
        return nullptr;
    }
    SDL_Log("text: %s font %s (%s)", role, path, TTF_GetFontFamilyName(font));
    return font;
}

} // namespace

struct TextCache::Fonts {
    TTF_Font* body = nullptr;
    TTF_Font* title = nullptr;
    TTF_Font* fallback = nullptr;
};

TextCache::TextCache(Renderer& renderer) : renderer_(renderer), fonts_(std::make_unique<Fonts>()) {}

bool TextCache::ready() const {
    return fonts_->body || fonts_->title;
}

TextCache::~TextCache() {
    shutdown();
}

bool TextCache::init(const char* bodyPath, const char* titlePath, int pixelGrid, const char* fallbackPath) {
    if (!TTF_Init()) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "text: TTF_Init failed: %s", SDL_GetError());
        return false;
    }
    ttfStarted_ = true;
    const int version = TTF_Version();
    SDL_Log("text: SDL3_ttf %d.%d.%d", SDL_VERSIONNUM_MAJOR(version), SDL_VERSIONNUM_MINOR(version),
            SDL_VERSIONNUM_MICRO(version));
    pixelGrid_ = std::max(0, pixelGrid);
    const float openSize = pixelGrid_ > 0 ? static_cast<float>(pixelGrid_) : 24.0f;
    fonts_->body = openFont(bodyPath, "body", openSize);
    fonts_->title = openFont(titlePath, "title", openSize);
    if (pixelGrid_ > 0) {
        // Outlines already sit on whole pixels at the grid size; hinting
        // would only move them.
        for (TTF_Font* font : {fonts_->body, fonts_->title}) {
            if (font) {
                TTF_SetFontHinting(font, TTF_HINTING_NONE);
            }
        }
        fonts_->fallback = openFont(fallbackPath, "fallback", openSize);
        for (TTF_Font* font : {fonts_->body, fonts_->title}) {
            if (font && fonts_->fallback && !TTF_AddFallbackFont(font, fonts_->fallback)) {
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text: TTF_AddFallbackFont failed: %s", SDL_GetError());
            }
        }
    }
    if (!fonts_->body && fonts_->title) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text: body text uses the title font");
    } else if (fonts_->body && !fonts_->title) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text: titles use the body font");
    } else if (!fonts_->body && !fonts_->title) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "text: no font loaded; the UI draws without text");
        return false;
    }
    entries_.reserve(64);
    return true;
}

void TextCache::shutdown() {
    // Textures before the fonts and the renderer.
    entries_.clear();
    if (fonts_->body) {
        TTF_CloseFont(fonts_->body);
        fonts_->body = nullptr;
    }
    if (fonts_->title) {
        TTF_CloseFont(fonts_->title);
        fonts_->title = nullptr;
    }
    // After the fonts that use it as a fallback.
    if (fonts_->fallback) {
        TTF_CloseFont(fonts_->fallback);
        fonts_->fallback = nullptr;
    }
    if (ttfStarted_) {
        TTF_Quit();
        ttfStarted_ = false;
    }
}

void* TextCache::fontFor(FontFace face, int sizePx) {
    TTF_Font* font = face == FontFace::Title ? (fonts_->title ? fonts_->title : fonts_->body) : (fonts_->body ? fonts_->body : fonts_->title);
    if (!font) {
        return nullptr;
    }
    // A pixel font stays at its grid size; the text is scaled when drawn.
    if (pixelGrid_ > 0) {
        return font;
    }
    // One TTF_Font per face; the size changes per render. SDL_ttf points at
    // its default 72 DPI are pixels.
    if (TTF_GetFontSize(font) != static_cast<float>(sizePx) && !TTF_SetFontSize(font, static_cast<float>(sizePx))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text: TTF_SetFontSize(%d) failed: %s", sizePx, SDL_GetError());
    }
    return font;
}

float TextCache::scaleFor(int sizePx) const {
    return pixelGrid_ > 0 ? static_cast<float>(std::clamp(sizePx, kMinSizePx, kMaxSizePx)) / kPixelSizePerGrid : 1.0f;
}

const TextCache::Entry* TextCache::find(FontFace face, int sizePx, std::string_view text, int wrapPx) {
    sizePx = std::clamp(sizePx, kMinSizePx, kMaxSizePx);
    wrapPx = std::max(wrapPx, 0);
    for (Entry& entry : entries_) {
        if (entry.sizePx == sizePx && entry.wrapPx == wrapPx && entry.face == face && entry.text == text) {
            entry.lastUsed = frame_;
            return &entry;
        }
    }
    if (text.empty()) {
        return nullptr;
    }
    TTF_Font* font = static_cast<TTF_Font*>(fontFor(face, sizePx));
    if (!font) {
        return nullptr;
    }
    // White, tinted at draw time, so one texture serves every color.
    constexpr SDL_Color kWhite{255, 255, 255, 255};
    const float scale = scaleFor(sizePx);
    const int renderWrap = wrapPx > 0 ? std::max(1, static_cast<int>(static_cast<float>(wrapPx) / scale)) : 0;
    SDL_Surface* surface = renderWrap > 0
                               ? TTF_RenderText_Blended_Wrapped(font, text.data(), text.size(), kWhite, renderWrap)
                               : TTF_RenderText_Blended(font, text.data(), text.size(), kWhite);
    if (!surface) {
        if (!warnedRender_) {
            warnedRender_ = true;
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "text: TTF_RenderText_Blended failed: %s (logged once)",
                        SDL_GetError());
        }
        return nullptr;
    }
    Entry entry;
    entry.face = face;
    entry.sizePx = sizePx;
    entry.wrapPx = wrapPx;
    entry.text.assign(text);
    // The drawn size; a pixel font's texture is scaled up to it as pixel art.
    entry.size = TextSize{static_cast<int>(std::lround(static_cast<float>(surface->w) * scale)),
                          static_cast<int>(std::lround(static_cast<float>(surface->h) * scale))};
    entry.texture = renderer_.createTextureFromSurface(
        surface, pixelGrid_ > 0 ? TextureFilter::PixelArt : TextureFilter::Linear);
    entry.lastUsed = frame_;
    SDL_DestroySurface(surface);
    ++renders_;
    if (entries_.size() >= kMaxEntries) {
        auto oldest = std::min_element(entries_.begin(), entries_.end(),
                                       [](const Entry& a, const Entry& b) { return a.lastUsed < b.lastUsed; });
        *oldest = std::move(entry);
        return &*oldest;
    }
    entries_.push_back(std::move(entry));
    return &entries_.back();
}

TextSize TextCache::measure(FontFace face, int sizePx, std::string_view text, int wrapPx) {
    const Entry* entry = find(face, sizePx, text, wrapPx);
    return entry ? entry->size : TextSize{};
}

TextSize TextCache::draw(FontFace face, int sizePx, std::string_view text, float x, float y, Color color,
                         int wrapPx) {
    const Entry* entry = find(face, sizePx, text, wrapPx);
    if (!entry) {
        return {};
    }
    renderer_.drawScreenTexture(entry->texture,
                                ScreenRect{x, y, static_cast<float>(entry->size.w), static_cast<float>(entry->size.h)},
                                color);
    return entry->size;
}

TextSize TextCache::drawGradient(FontFace face, int sizePx, std::string_view text, float x, float y, Color top,
                                 Color bottom) {
    const Entry* entry = find(face, sizePx, text, 0);
    if (!entry) {
        return {};
    }
    renderer_.drawScreenTextureGradient(
        entry->texture, ScreenRect{x, y, static_cast<float>(entry->size.w), static_cast<float>(entry->size.h)}, top,
        bottom);
    return entry->size;
}

std::size_t TextCache::fitBytes(FontFace face, int sizePx, std::string_view text, int maxWidthPx) {
    TTF_Font* font = static_cast<TTF_Font*>(fontFor(face, std::clamp(sizePx, kMinSizePx, kMaxSizePx)));
    if (!font || text.empty() || maxWidthPx <= 0) {
        return 0;
    }
    int width = 0;
    size_t length = 0;
    const int limit = std::max(1, static_cast<int>(static_cast<float>(maxWidthPx) / scaleFor(sizePx)));
    if (!TTF_MeasureString(font, text.data(), text.size(), limit, &width, &length)) {
        return 0;
    }
    length = std::min(length, text.size());
    // SDL_ttf counts whole characters; keep it on a boundary anyway.
    while (length > 0 && length < text.size() && (static_cast<unsigned char>(text[length]) & 0xC0) == 0x80) {
        --length;
    }
    return length;
}

int TextCache::lineHeight(FontFace face, int sizePx) {
    TTF_Font* font = static_cast<TTF_Font*>(fontFor(face, std::clamp(sizePx, kMinSizePx, kMaxSizePx)));
    return font ? static_cast<int>(std::lround(static_cast<float>(TTF_GetFontHeight(font)) * scaleFor(sizePx))) : sizePx;
}

void TextCache::endFrame() {
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [this](const Entry& entry) { return frame_ - entry.lastUsed > kEvictAfterFrames; }),
                   entries_.end());
    ++frame_;
}

} // namespace vibe

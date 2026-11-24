#include "vibe/ui/Ui.h"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>

#include <algorithm>
#include <cmath>

#include "vibe/platform/Device.h"
#include "vibe/platform/Fullscreen.h"
#include "vibe/platform/Paste.h"
#include "vibe/platform/TouchText.h"
#ifdef VIBE_DEV
#include "vibe/ui/dev/DevHooks.h"
#endif

namespace vibe::ui {
namespace {

// Bytes of the UTF-8 character that ends `text`.
std::size_t lastCharBytes(const std::string& text) {
    std::size_t n = 1;
    while (n < text.size() && (static_cast<unsigned char>(text[text.size() - n]) & 0xC0) == 0x80) {
        ++n;
    }
    return std::min(n, text.size());
}

// Characters in UTF-8 `text` (lead bytes).
std::size_t charCount(std::string_view text) {
    std::size_t n = 0;
    for (const char c : text) {
        n += (static_cast<unsigned char>(c) & 0xC0) != 0x80 ? 1 : 0;
    }
    return n;
}

} // namespace

void Ui::beginFrame() {
    const Viewport& viewport = renderer_.viewport();
    width_ = static_cast<float>(viewport.widthPx);
    height_ = static_cast<float>(viewport.heightPx);
    int cssWidth = 0;
    int cssHeight = 0;
    if (renderer_.window() && SDL_GetWindowSize(renderer_.window(), &cssWidth, &cssHeight) && cssWidth > 0) {
        pixelRatio_ = width_ / static_cast<float>(cssWidth);
    }
    // Plan 14: lay out inside the safe area. At most a third of the canvas
    // goes to insets, whatever the page reports.
    const platform::SafeInsets in = platform::safeAreaInsets();
    const float l = std::clamp(in.left * pixelRatio_, 0.0f, width_ / 3.0f);
    const float r = std::clamp(in.right * pixelRatio_, 0.0f, width_ / 3.0f);
    const float t = std::clamp(in.top * pixelRatio_, 0.0f, height_ / 3.0f);
    const float b = std::clamp(in.bottom * pixelRatio_, 0.0f, height_ / 3.0f);
    safe_ = ScreenRect{std::round(l), std::round(t), std::round(width_ - l - r), std::round(height_ - t - b)};
    scale_ = std::max(0.25f, std::min(safe_.w / kDesignWidth, safe_.h / kDesignHeight));
}

ScreenRect Ui::centered(float widthUnits, float heightUnits) const {
    const float w = std::min(u(widthUnits), safe_.w);
    const float h = std::min(u(heightUnits), safe_.h);
    return ScreenRect{safe_.x + std::floor((safe_.w - w) * 0.5f), safe_.y + std::floor((safe_.h - h) * 0.5f), w, h};
}

int Ui::fontPx(float sizeUnits) const {
    return std::max(8, static_cast<int>(std::lround(sizeUnits * scale_)));
}

TextSize Ui::text(FontFace face, float sizeUnits, std::string_view text, float x, float y, Color color, Align align) {
    const int px = fontPx(sizeUnits);
    if (align != Align::Left) {
        const TextSize size = text_.measure(face, px, text);
        x -= align == Align::Center ? static_cast<float>(size.w) * 0.5f : static_cast<float>(size.w);
    }
    return text_.draw(face, px, text, std::round(x), std::round(y), color);
}

TextSize Ui::fitText(FontFace face, float sizeUnits, std::string_view text, float x, float y, float maxWidth,
                     Color color, Align align) {
    const TextSize size = text_.measure(face, fontPx(sizeUnits), text);
    if (size.w > 0 && static_cast<float>(size.w) > maxWidth && maxWidth > 0.0f) {
        sizeUnits *= maxWidth / static_cast<float>(size.w);
    }
    return this->text(face, sizeUnits, text, x, y, color, align);
}

float Ui::paragraph(FontFace face, float sizeUnits, std::string_view text, float x, float y, float wrapWidth,
                    Color color) {
    if (text.empty()) {
        return 0.0f;
    }
    const TextSize size = text_.draw(face, fontPx(sizeUnits), text, std::round(x), std::round(y), color,
                                     std::max(1, static_cast<int>(wrapWidth)));
    return static_cast<float>(size.h);
}

void Ui::panel(const ScreenRect& rect) {
    style::panel(*this, rect);
}

void Ui::loadIcons() {
    struct Source {
        Icon icon;
        const char* path;
        bool mask;
        bool pixel = false;
    };
    static constexpr Source kSources[] = {
        {Icon::User, "assets/ui/art/glyph-user.png", true},
        {Icon::Lock, "assets/ui/art/glyph-lock.png", true},
        {Icon::ChevronLeft, "assets/ui/art/glyph-chevron-left.png", true},
        {Icon::ChevronRight, "assets/ui/art/glyph-chevron-right.png", true},
        {Icon::Gear, "assets/ui/art/glyph-gear.png", true},
        {Icon::Refresh, "assets/ui/art/glyph-refresh.png", true},
        {Icon::Exit, "assets/ui/art/glyph-exit.png", true},
        {Icon::Music, "assets/ui/art/glyph-music.png", true},
        {Icon::MusicOff, "assets/ui/art/glyph-music-off.png", true},
        {Icon::Sound, "assets/ui/art/glyph-sound.png", true},
        {Icon::SoundOff, "assets/ui/art/glyph-sound-off.png", true},
        {Icon::Plus, "assets/ui/art/glyph-plus.png", true},
        {Icon::Chat, "assets/ui/art/glyph-chat.png", true},
        {Icon::Send, "assets/ui/art/glyph-send.png", true},
        {Icon::Check, "assets/ui/art/glyph-check.png", true},
        {Icon::Star, "assets/ui/art/glyph-star.png", true},
        {Icon::Fullscreen, "assets/ui/art/glyph-fullscreen.png", true},
        {Icon::FullscreenExit, "assets/ui/art/glyph-fullscreen-exit.png", true},
        {Icon::Diamond, "assets/ui/art/diamond.png", false},
        {Icon::Battle, "assets/ui/art/battle.png", false},
        {Icon::Trophy, "assets/ui/art/trophy.png", false},
        {Icon::Podium, "assets/ui/art/podium.png", false},
        {Icon::MedalGold, "assets/ui/art/medal-gold.png", false},
        {Icon::MedalSilver, "assets/ui/art/medal-silver.png", false},
        {Icon::MedalBronze, "assets/ui/art/medal-bronze.png", false},
        {Icon::Hammer, "assets/ui/art/hammer-SH.png", false},
        {Icon::IronHammer, "assets/ui/art/hammer-IH.png", false},
        {Icon::Logo, "assets/ui/art/logo.png", false, true},
        {Icon::Level, "assets/ui/art/level.png", false, true},
        {Icon::AvatarGold, "assets/ui/art/avatar-gold.png", false, true},
        {Icon::AvatarSilver, "assets/ui/art/avatar-silver.png", false, true},
    };
    for (const Source& source : kSources) {
        icons_[static_cast<std::size_t>(source.icon)] =
            source.mask    ? renderer_.loadPngMask(source.path, TextureFilter::PixelArt)
            : source.pixel ? renderer_.loadPng(source.path, TextureFilter::PixelArt)
                           : renderer_.loadPng(source.path);
    }
}

void Ui::releaseIcons() {
    for (Texture& texture : icons_) {
        texture = Texture();
    }
    for (Texture& texture : frames_) {
        texture = Texture();
    }
    frameFailed_ = {};
}

namespace {

// Each frame's file and its corner size in art pixels (the border that keeps
// its shape; tools/gen_ui_art.py draws rivets, bevels and lips inside it).
struct FrameSource {
    Frame frame;
    const char* path;
    float corner;
};
constexpr FrameSource kFrames[] = {
    {Frame::Panel, "assets/ui/art/frame-panel.png", 8.0f},
    {Frame::Card, "assets/ui/art/frame-card.png", 5.0f},
    {Frame::CardGold, "assets/ui/art/frame-card-gold.png", 5.0f},
    {Frame::ButtonGold, "assets/ui/art/button-gold.png", 6.0f},
    {Frame::ButtonGoldHover, "assets/ui/art/button-gold-hover.png", 6.0f},
    {Frame::ButtonGoldPressed, "assets/ui/art/button-gold-pressed.png", 6.0f},
    {Frame::ButtonBlue, "assets/ui/art/button-blue.png", 6.0f},
    {Frame::ButtonBlueHover, "assets/ui/art/button-blue-hover.png", 6.0f},
    {Frame::ButtonBluePressed, "assets/ui/art/button-blue-pressed.png", 6.0f},
    {Frame::ButtonDark, "assets/ui/art/button-dark.png", 6.0f},
    {Frame::ButtonDarkHover, "assets/ui/art/button-dark-hover.png", 6.0f},
    {Frame::ButtonDarkPressed, "assets/ui/art/button-dark-pressed.png", 6.0f},
    {Frame::ButtonDisabled, "assets/ui/art/button-disabled.png", 6.0f},
    {Frame::Field, "assets/ui/art/field.png", 5.0f},
    {Frame::FieldFocus, "assets/ui/art/field-focus.png", 5.0f},
    {Frame::Round, "assets/ui/art/button-round.png", 0.0f},
    {Frame::RoundHover, "assets/ui/art/button-round-hover.png", 0.0f},
    {Frame::RoundPressed, "assets/ui/art/button-round-pressed.png", 0.0f},
    {Frame::RoundDisabled, "assets/ui/art/button-round-disabled.png", 0.0f},
};

} // namespace

void Ui::frame(Frame which, const ScreenRect& rect, Color tint) {
    const std::size_t i = static_cast<std::size_t>(which);
    if (!frames_[i].valid() && !frameFailed_[i]) {
        // Loaded on first use, sampled as pixel art.
        frames_[i] = renderer_.loadPng(kFrames[i].path, TextureFilter::PixelArt);
        frameFailed_[i] = !frames_[i].valid();
    }
    if (!frames_[i].valid()) {
        renderer_.fillScreenRect(rect, Color{20, 24, 46, 240});
        return;
    }
    if (kFrames[i].corner <= 0.0f) {
        renderer_.drawScreenTexture(frames_[i], rect, tint);
        return;
    }
    renderer_.drawScreenNineSlice(frames_[i], rect, kFrames[i].corner, std::max(1.0f, u(kPixel)), tint);
}

float Ui::iconAspect(Icon icon) const {
    const Texture& texture = icons_[static_cast<std::size_t>(icon)];
    return texture.valid() && texture.height() > 0 ? static_cast<float>(texture.width()) / static_cast<float>(texture.height())
                                                   : 1.0f;
}

void Ui::icon(Icon icon, const ScreenRect& rect, Color tint) {
    const Texture& texture = icons_[static_cast<std::size_t>(icon)];
    if (!texture.valid() || rect.w <= 0.0f || rect.h <= 0.0f) {
        return;
    }
    const float k = std::min(rect.w / static_cast<float>(texture.width()), rect.h / static_cast<float>(texture.height()));
    const float w = static_cast<float>(texture.width()) * k;
    const float h = static_cast<float>(texture.height()) * k;
    renderer_.drawScreenTexture(texture, ScreenRect{rect.x + (rect.w - w) * 0.5f, rect.y + (rect.h - h) * 0.5f, w, h},
                                tint);
}

void Ui::focus(TextField* field) {
    if (field == focused_) {
        return;
    }
    focused_ = field;
    SDL_Window* window = renderer_.window();
    if (!window) {
        return;
    }
    if (field) {
        const SDL_PropertiesID props = SDL_CreateProperties();
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_TYPE_NUMBER,
                              field->password() ? SDL_TEXTINPUT_TYPE_TEXT_PASSWORD_HIDDEN
                                                : SDL_TEXTINPUT_TYPE_TEXT_USERNAME);
        SDL_SetNumberProperty(props, SDL_PROP_TEXTINPUT_CAPITALIZATION_NUMBER, SDL_CAPITALIZE_NONE);
        SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_AUTOCORRECT_BOOLEAN, false);
        const bool started = SDL_StartTextInputWithProperties(window, props);
        SDL_DestroyProperties(props);
        if (!started) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui: SDL_StartTextInput failed: %s", SDL_GetError());
        }
    } else if (SDL_TextInputActive(window)) {
        if (!SDL_StopTextInput(window)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui: SDL_StopTextInput failed: %s", SDL_GetError());
        }
    }
}

void Ui::takePaste() {
    if (!platform::takePastedText(paste_)) {
        return;
    }
    if (focused_) {
        focused_->insert(paste_);
        if (platform::active()) {
            // The proxy mirrors the field; keep it in step.
            platform::setValue(focused_->text(), static_cast<int>(focused_->maxBytes()));
        }
    }
    std::fill(paste_.begin(), paste_.end(), '\0');
    paste_.clear();
}

void Ui::registerField(TextField* field) {
    if (fieldCount_ < static_cast<int>(fields_.size())) {
        fields_[static_cast<std::size_t>(fieldCount_++)] = field;
    }
}

void Ui::publishTouchFields() {
    // The game took the focus away: close the on-screen keyboard. Checked
    // once per frame, not in focus(): a tap from one field to another
    // unfocuses the first before it focuses the second.
    if (!focused_ && platform::active()) {
        platform::blur();
    }
    std::array<platform::FieldRect, 4> rects{};
    const int count = touchMode_ ? fieldCount_ : 0;
    for (int i = 0; i < count; ++i) {
        const TextField* f = fields_[static_cast<std::size_t>(i)];
        const ScreenRect& r = f->rect();
        rects[static_cast<std::size_t>(i)] = platform::FieldRect{r.x, r.y, r.w, r.h, f->password()};
    }
    publishedFields_ = fields_;
    publishedCount_ = count;
    platform::publishFields(rects.data(), count);
    fieldCount_ = 0;
    const ScreenRect& b = fullscreenButton_;
    platform::publishButton(b.x, b.y, b.w, b.h);
    fullscreenButton_ = ScreenRect{};
}

bool fullscreenButtonShown() {
    return !platform::runningAsApp() && (platform::fullscreenSupported() || platform::isIphone());
}

const char* fullscreenTip() {
    return platform::isIphone() ? "For full screen on iPhone: tap Share, then Add to Home Screen."
                                : "This browser cannot play full screen.";
}

void Ui::takeTouchText() {
    const int index = platform::takeFocus();
    if (index >= 0 && index < publishedCount_) {
        TextField* field = publishedFields_[static_cast<std::size_t>(index)];
        focus(field);
        platform::setValue(field->text(), static_cast<int>(field->maxBytes()));
        SDL_Log("ui: on-screen keyboard for %s", field->password() ? "a password field" : "a text field");
    }
    if (platform::takeValue(typed_)) {
        if (focused_ && platform::active()) {
            focused_->setText(typed_);
        }
        std::fill(typed_.begin(), typed_.end(), '\0');
        typed_.clear();
    }
    if (platform::takeEnter() && focused_) {
        // The screens already act on Enter from a key; post the same key.
        SDL_Event enter{};
        enter.type = SDL_EVENT_KEY_DOWN;
        enter.key.timestamp = SDL_GetTicksNS();
        enter.key.windowID = renderer_.window() ? SDL_GetWindowID(renderer_.window()) : 0;
        enter.key.scancode = SDL_SCANCODE_RETURN;
        enter.key.key = SDLK_RETURN;
        enter.key.down = true;
        if (!SDL_PushEvent(&enter)) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "ui: SDL_PushEvent(Enter) failed: %s", SDL_GetError());
        }
    }
    if (platform::takeBlur() && focused_ && !platform::active()) {
        // The keyboard was closed (Done, or a tap outside the field).
        focus(nullptr);
    }
}

void Ui::mark([[maybe_unused]] const char* id, [[maybe_unused]] const ScreenRect& rect) {
#ifdef VIBE_DEV
    dev::markRect(id, rect);
#endif
}

// ---- Button -----------------------------------------------------------------

bool Button::onEvent(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        hover_ = rect_.contains(event.motion.x, event.motion.y);
        return false;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            pressed_ = enabled_ && rect_.contains(event.button.x, event.button.y);
        }
        return false;
    case SDL_EVENT_MOUSE_BUTTON_UP: {
        if (event.button.button != SDL_BUTTON_LEFT) {
            return false;
        }
        const bool clicked = pressed_ && enabled_ && rect_.contains(event.button.x, event.button.y);
        pressed_ = false;
        return clicked;
    }
    default:
        return false;
    }
}

void Button::draw(Ui& ui, const ScreenRect& rect, ButtonStyle style, float textUnits) {
    rect_ = rect;
    ui.mark(id_.c_str(), rect);
    const bool lit = hover_ || active_;
    style::buttonFace(ui, rect, style, lit, pressed_, enabled_);
    if (style == ButtonStyle::Round) {
        if (hasIcon_) {
            // On the disc's pixel grid, inset 3 art pixels; the disc drops
            // onto its lip when pressed.
            const ScreenRect art = style::roundSprite(rect);
            const float p = art.w / kRoundArtWidth;
            const float inset = (kRoundArtWidth - kRoundGlyph) * 0.5f * p;
            const float top = art.y + inset + (pressed_ ? kRoundLip * p : 0.0f);
            const Color c = !enabled_ ? palette::kDim : lit ? palette::kCream : palette::kGoldLight;
            ui.icon(icon_, ScreenRect{art.x + inset, top, kRoundGlyph * p, kRoundGlyph * p}, c);
        }
        return;
    }
    const float textHeight = static_cast<float>(ui.textCache().lineHeight(FontFace::Title, ui.fontPx(textUnits)));
    // The pixel buttons have a 2-pixel lip along the bottom: center the text
    // on the face above it, and drop it onto the lip when pressed.
    const bool pixelFace = style == ButtonStyle::Gold || style == ButtonStyle::Blue || style == ButtonStyle::Dark;
    const float lip = pixelFace ? ui.u(kPixel) : 0.0f;
    const float y = rect.y + (rect.h - textHeight) * 0.5f - lip + (pressed_ ? lip * 1.5f : 0.0f);
    if (style == ButtonStyle::Link) {
        // Text only: gold, lighter and underlined on hover.
        const Color c = !enabled_ ? palette::kDim : hover_ ? palette::kGoldLight : palette::kGold;
        const TextSize ts = ui.fitText(FontFace::Body, textUnits, label_, rect.x + rect.w * 0.5f, y + lip, rect.w, c,
                                       Align::Center);
        if (hover_ && enabled_) {
            ui.renderer().fillScreenRect(ScreenRect{rect.x + (rect.w - static_cast<float>(ts.w)) * 0.5f,
                                                    y + lip + static_cast<float>(ts.h) - ui.u(1.0f),
                                                    static_cast<float>(ts.w), std::max(1.0f, ui.u(1.5f))},
                                         c);
        }
        return;
    }
    Color color = palette::kCream;
    if (!enabled_) {
        color = Color{120, 124, 140, 255};
    } else if (style == ButtonStyle::Gold) {
        color = Color{62, 34, 6, 255};
    }
    if (hasIcon_ && pixelFace && label_.empty()) {
        // A glyph instead of a label, 2 design units per glyph pixel (whole
        // canvas pixels), centered on the face above the lip.
        const float p = std::max(1.0f, std::round(ui.u(2.0f)));
        const float g = kRoundGlyph * p;
        const float gy = rect.y + (rect.h - g) * 0.5f - lip * 0.5f + (pressed_ ? lip : 0.0f);
        ui.icon(icon_, ScreenRect{std::round(rect.x + (rect.w - g) * 0.5f), std::round(gy), g, g}, color);
        return;
    }
    if (enabled_ && style != ButtonStyle::Gold) {
        // Light text gets a dark shadow; dark text on gold gets a light one.
        ui.fitText(FontFace::Title, textUnits, label_, rect.x + rect.w * 0.5f + ui.u(1.5f), y + ui.u(1.5f),
                   rect.w - rect.h, Color{0, 0, 0, 150}, Align::Center);
    } else if (enabled_) {
        ui.fitText(FontFace::Title, textUnits, label_, rect.x + rect.w * 0.5f, y + ui.u(1.5f), rect.w - rect.h,
                   Color{255, 246, 210, 150}, Align::Center);
    }
    ui.fitText(FontFace::Title, textUnits, label_, rect.x + rect.w * 0.5f, y, rect.w - rect.h, color, Align::Center);
}

// ---- TextField --------------------------------------------------------------

TextField::Result TextField::onEvent(Ui& ui, const SDL_Event& event) {
    const bool focused = ui.focused() == this;
    switch (event.type) {
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (event.button.button == SDL_BUTTON_LEFT) {
            if (rect_.contains(event.button.x, event.button.y)) {
                ui.focus(this);
            } else if (focused) {
                ui.focus(nullptr);
            }
        }
        return Result::None;
    case SDL_EVENT_TEXT_INPUT:
        if (focused && event.text.text) {
            insert(event.text.text);
        }
        return Result::None;
    case SDL_EVENT_KEY_DOWN:
        if (!focused) {
            return Result::None;
        }
        switch (event.key.key) {
        case SDLK_BACKSPACE:
            if (!text_.empty()) {
                const std::size_t n = lastCharBytes(text_);
                std::fill(text_.end() - static_cast<std::ptrdiff_t>(n), text_.end(), '\0');
                text_.resize(text_.size() - n);
            }
            return Result::None;
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            return event.key.repeat ? Result::None : Result::Submit;
        case SDLK_TAB:
            return Result::Next;
        default:
            return Result::None;
        }
    default:
        return Result::None;
    }
}

void TextField::insert(std::string_view text) {
    for (const char c : text) {
        // One line: no control characters (a pasted newline or tab).
        if (static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            continue;
        }
        if (text_.size() >= maxBytes_) {
            break;
        }
        text_.push_back(c);
    }
    // Never leave a cut UTF-8 character at the end.
    while (!text_.empty() && (static_cast<unsigned char>(text_.back()) & 0x80) != 0) {
        const std::size_t n = lastCharBytes(text_);
        const unsigned char lead = static_cast<unsigned char>(text_[text_.size() - n]);
        const std::size_t want = lead >= 0xF0 ? 4 : lead >= 0xE0 ? 3 : lead >= 0xC0 ? 2 : 1;
        if (want == n) {
            break;
        }
        text_.resize(text_.size() - n);
    }
}

void TextField::setText(std::string_view text) {
    wipe();
    insert(text);
}

void TextField::wipe() {
    std::fill(text_.begin(), text_.end(), '\0');
    text_.clear();
    std::fill(shown_.begin(), shown_.end(), '\0');
    shown_.clear();
}

void TextField::draw(Ui& ui, const ScreenRect& rect) {
    rect_ = rect;
    ui.mark(id_, rect);
    ui.registerField(this);
    const bool focused = ui.focused() == this;
    Renderer& r = ui.renderer();
    ui.frame(focused ? Frame::FieldFocus : Frame::Field, rect);
    const float size = textUnits_;
    float pad = ui.u(12.0f);
    const float right = pad;
    const float textHeight = static_cast<float>(ui.textCache().lineHeight(FontFace::Body, ui.fontPx(size)));
    const float y = rect.y + (rect.h - textHeight) * 0.5f;
    if (hasIcon_) {
        const float k = rect.h * 0.44f;
        ui.icon(icon_, ScreenRect{rect.x + pad + ui.u(2.0f), rect.y + (rect.h - k) * 0.5f, k, k},
                focused ? palette::kGold : palette::kSilver);
        // The text starts after the icon (fields with no icon keep their pad).
        pad += k + ui.u(10.0f);
    }
    if (text_.empty()) {
        if (!focused) {
            ui.fitText(FontFace::Body, size, placeholder_, rect.x + pad, y, rect.w - pad - right, palette::kDim);
        }
    } else {
        // Re-built only when the text changed, so the cache key is stable.
        const std::size_t chars = charCount(text_);
        if (password_) {
            if (shown_.size() != chars) {
                shown_.assign(chars, '*');
            }
        } else if (shown_ != text_) {
            shown_ = text_;
        }
    }
    float caretX = rect.x + pad;
    if (!text_.empty()) {
        // Keep the end of long text in view: shift it left inside a clip.
        const TextSize shown = ui.textCache().measure(FontFace::Body, ui.fontPx(size), shown_);
        const float room = rect.w - pad - right;
        const float x = rect.x + pad - std::max(0.0f, static_cast<float>(shown.w) - room);
        const float clipLeft = hasIcon_ ? pad - ui.u(4.0f) : pad * 0.5f;
        r.setScreenClip(ScreenRect{rect.x + clipLeft, rect.y, rect.w - clipLeft - right * 0.5f, rect.h});
        ui.text(FontFace::Body, size, shown_, x, y, palette::kCream);
        r.clearScreenClip();
        caretX = x + static_cast<float>(shown.w) + ui.u(2.0f);
    }
    if (focused && (SDL_GetTicks() / 500) % 2 == 0) {
        r.fillScreenRect(ScreenRect{std::round(caretX), y + textHeight * 0.1f, std::max(1.0f, ui.u(2.5f)),
                                    textHeight * 0.8f},
                         palette::kGold);
    }
}

} // namespace vibe::ui

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

#include <array>
#include <cmath>

#include "vibe/core/Input.h"
#include "vibe/render/Renderer.h"
#include "vibe/render/Text.h"
#include "vibe/render/Texture.h"
#include "vibe/ui/Style.h"

// Plan 11's shipping UI: a small retained widget set drawn through
// src/render/ (decision D5). Screens own their widgets; each frame lays them
// out from the canvas size and draws them, and events hit-test the rectangles
// drawn last. Every lobby rule stays in lobby::LobbyController.
namespace vibe::ui {

// Layout is in units of a 1280x720 design, scaled to the canvas so the
// layout looks the same at any size and devicePixelRatio, and fits a
// 960x540 window.
inline constexpr float kDesignWidth = 1280.0f;
inline constexpr float kDesignHeight = 720.0f;


enum class Align { Left, Center, Right };

struct TextStyle {
    FontFace face = FontFace::Body;
    float size = 16.0f;
    Color color = palette::kCream;
    Align align = Align::Left;
    float maxWidth = 0.0f;
    float wrapWidth = 0.0f;
    bool shadow = false;
};

// Icons from assets/ui/ (plan 04). The single-color ones are loaded as
// white masks and tinted; the art ones keep their colors (tint white).
// The UI's icons (2026-10-22): assets/ui/art/, made by tools/gen_ui_art.py in
// the UI's gilded style. Glyphs are white masks tinted at draw time; the
// color icons keep their colors (tint white).
enum class Icon {
    // Glyphs
    User,
    Lock,
    ChevronLeft,
    ChevronRight,
    Gear,
    Refresh,
    Exit,
    Music,
    MusicOff,
    Sound,
    SoundOff,
    Plus,
    Chat,
    Send,
    Check,
    Star,
    Fullscreen,
    FullscreenExit,
    // Color icons
    Diamond,
    Battle,
    Trophy,
    Podium,
    MedalGold,
    MedalSilver,
    MedalBronze,
    Hammer,
    IronHammer,
    // Pixel art (sampled as pixel art)
    Logo,
    Level,
    AvatarGold,
    AvatarSilver,
    Count
};

// The pixel-art nine-slice frames (assets/ui/art/, tools/gen_ui_art.py),
// drawn at kPixel design units per art pixel so every frame has the same
// pixel size as the others and as the logo.
enum class Frame {
    Panel,
    Card,
    CardGold,
    ButtonGold,
    ButtonGoldHover,
    ButtonGoldPressed,
    ButtonBlue,
    ButtonBlueHover,
    ButtonBluePressed,
    ButtonDark,
    ButtonDarkHover,
    ButtonDarkPressed,
    ButtonDisabled,
    Field,
    FieldFocus,
    // Drawn whole, fitted to the rectangle (not nine-sliced).
    Round,
    RoundHover,
    RoundPressed,
    RoundDisabled,
    Count
};

// Design units per art pixel of the frames.
inline constexpr float kPixel = 3.0f;
// The round button art (tools/gen_ui_art.py): a disc and the lip under it,
// and the glyph inset on the disc, all in art pixels.
inline constexpr float kRoundArtWidth = 21.0f;
inline constexpr float kRoundArtHeight = 23.0f;
inline constexpr float kRoundLip = 2.0f;
inline constexpr float kRoundGlyph = 11.0f;
// The avatar ring (a disc, no lip) and the level hexagon, in art pixels.
inline constexpr float kAvatarArt = 25.0f;
inline constexpr float kLevelArtWidth = 15.0f;
inline constexpr float kLevelArtHeight = 16.0f;

class TextField;

// Per-frame drawing context and text-input focus. One per scene.
class Ui {
public:
    Ui(Renderer& renderer, TextCache& text) : renderer_(renderer), text_(text) {}

    // Reads the canvas size. Call once per frame before laying out.
    void beginFrame();
    Renderer& renderer() { return renderer_; }
    TextCache& textCache() { return text_; }

    // The whole canvas, for backgrounds and dimming that fill the page.
    float width() const { return width_; }
    float height() const { return height_; }
    // Plan 14: the safe area (canvas minus the notch, rounded corners and
    // home indicator, platform::safeAreaInsets). Widgets, the design frame
    // and the scale keep inside it. It is the whole canvas on desktop.
    float left() const { return safe_.x; }
    float top() const { return safe_.y; }
    float right() const { return safe_.x + safe_.w; }
    float bottom() const { return safe_.y + safe_.h; }
    float cx() const { return safe_.x + safe_.w * 0.5f; }
    const ScreenRect& safe() const { return safe_; }
    // Canvas pixels per CSS pixel.
    float pixelRatio() const { return pixelRatio_; }
    // How far a press moves before it is a drag (ui::DragScroll), in canvas
    // pixels: 8 CSS pixels, so it feels the same at any devicePixelRatio.
    float dragSlop() const { return 8.0f * pixelRatio_; }
    // Canvas pixels per design unit.
    float scale() const { return scale_; }
    float u(float units) const { return units * scale_; }
    // A point of the 1280x720 design frame, which is centered in the safe area.
    float dx(float units) const { return safe_.x + std::floor((safe_.w - kDesignWidth * scale_) * 0.5f) + units * scale_; }
    float dy(float units) const { return safe_.y + std::floor((safe_.h - kDesignHeight * scale_) * 0.5f) + units * scale_; }
    // A rectangle of design units, centered in the safe area.
    ScreenRect centered(float widthUnits, float heightUnits) const;

    // Text at `style.size` design units. `x` is the left edge, the center, or
    // the right edge by `style.align`; `y` is the top. A `maxWidth` shrinks
    // the line to fit, a `wrapWidth` wraps it, and `shadow` puts a soft dark
    // shadow under it.
    TextSize text(std::string_view text, float x, float y, const TextStyle& style);
    void panel(const ScreenRect& rect);

    // Loads the icons once (logs a missing file; that icon draws nothing).
    void loadIcons();
    void releaseIcons();
    // The icon fitted inside `rect`, keeping its aspect ratio.
    void icon(Icon icon, const ScreenRect& rect, Color tint);
    // A pixel-art frame stretched over `rect` (corners keep their shape).
    void frame(Frame frame, const ScreenRect& rect, Color tint = Color{255, 255, 255, 255});
    // An icon's texture size (0 x 0 when missing), for layout.
    float iconAspect(Icon icon) const;

    // The text field that gets typing, or none. Starts SDL text input while
    // a field has focus and stops it after (plan 11 step 6).
    void focus(TextField* field);
    TextField* focused() const { return focused_; }
    // Paste from the browser goes to the focused field (platform/Paste.h).
    void takePaste();

    // Plan 14: the on-screen keyboard (platform/TouchText.h). In touch mode
    // the fields drawn this frame are published to the page's text proxy,
    // and what the proxy took (focus, typing, Enter, blur) reaches the
    // fields here. Call takeTouchText once per frame before events are
    // used, and publishTouchFields after drawing.
    void setTouchMode(bool on) { touchMode_ = on; }
    bool touchMode() const { return touchMode_; }
    void registerField(TextField* field);
    void publishTouchFields();
    void takeTouchText();
    // The fullscreen button drawn this frame (platform/Fullscreen.h), sent
    // to the page with the touch fields; none when no screen marks one.
    void markFullscreenButton(const ScreenRect& rect) { fullscreenButton_ = rect; }

    // Dev builds: names a drawn rectangle so a page test can click it
    // (vibe_dev_ui_rect, src/ui/dev/). Nothing in Release.
    void mark(const char* id, const ScreenRect& rect);

    int fontPx(float sizeUnits) const;

private:
    Renderer& renderer_;
    TextCache& text_;
    TextField* focused_ = nullptr;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float scale_ = 1.0f;
    float pixelRatio_ = 1.0f;
    ScreenRect safe_;
    std::string paste_;
    bool touchMode_ = false;
    // Fields drawn this frame, and the list last published to the page
    // (platform::takeFocus indexes it).
    std::array<TextField*, 4> fields_{};
    int fieldCount_ = 0;
    std::array<TextField*, 4> publishedFields_{};
    int publishedCount_ = 0;
    std::string typed_;
    ScreenRect fullscreenButton_;
    std::array<Texture, static_cast<std::size_t>(Icon::Count)> icons_;
    std::array<Texture, static_cast<std::size_t>(Frame::Count)> frames_;
    // A frame whose file failed is not loaded again every frame.
    std::array<bool, static_cast<std::size_t>(Frame::Count)> frameFailed_{};
};

// A clickable button. Clicked on the release of a press that started on it.
class Button {
public:
    explicit Button(std::string id, std::string label = {}) : id_(std::move(id)), label_(std::move(label)) {}

    // `event` is in canvas pixels (Renderer::toCanvasCoordinates).
    bool onEvent(const InputEvent& event);
    void draw(Ui& ui, const ScreenRect& rect, ButtonStyle style = ButtonStyle::Dark, float textUnits = 22.0f);

    void setLabel(std::string_view label) {
        if (label_ != label) {
            label_.assign(label);
        }
    }
    void setId(std::string_view id) {
        if (id_ != id) {
            id_.assign(id);
        }
    }
    void setEnabled(bool enabled) { enabled_ = enabled; }
    bool enabled() const { return enabled_; }
    // A glyph drawn on a Round button (on the button art's pixel grid), or
    // in place of the label on a Gold, Blue or Dark one.
    void setIcon(Icon icon) {
        icon_ = icon;
        hasIcon_ = true;
    }
    // Drawn as hovered (an open menu's button).
    void setActive(bool active) { active_ = active; }
    // The mouse is over it (for tooltips and hover effects).
    bool hovered() const { return hover_ && enabled_; }
    bool held() const { return pressed_; }
    const ScreenRect& rect() const { return rect_; }

private:
    std::string id_;
    std::string label_;
    ScreenRect rect_;
    bool enabled_ = true;
    bool hover_ = false;
    bool pressed_ = false;
    bool active_ = false;
    bool hasIcon_ = false;
    Icon icon_ = Icon::Plus;
};

// A one-line text field fed by SDL text input events.
class TextField {
public:
    enum class Result { None, Submit, Next };

    TextField(const char* id, std::string placeholder, bool password, std::size_t maxBytes)
        : id_(id), placeholder_(std::move(placeholder)), password_(password), maxBytes_(maxBytes) {}
    ~TextField() { wipe(); }

    // `event` is in canvas pixels. A click inside focuses it; Enter submits;
    // Tab asks for the next field.
    Result onEvent(Ui& ui, const InputEvent& event);
    void draw(Ui& ui, const ScreenRect& rect);
    void insert(std::string_view text);

    // A glyph drawn at the left inside the field (the login's user and lock).
    void setIcon(Icon icon) {
        icon_ = icon;
        hasIcon_ = true;
    }
    // The text size in design units (default 24).
    void setTextUnits(float units) { textUnits_ = units; }
    const std::string& text() const { return text_; }
    bool empty() const { return text_.empty(); }
    // Overwrites the text with zeros before clearing it (passwords).
    void wipe();
    // Plan 14: replaces the text with what the on-screen keyboard's proxy
    // holds, under the same rules as insert().
    void setText(std::string_view text);
    bool password() const { return password_; }
    std::size_t maxBytes() const { return maxBytes_; }
    const ScreenRect& rect() const { return rect_; }

private:
    const char* id_;
    std::string placeholder_;
    bool password_;
    std::size_t maxBytes_;
    std::string text_;
    // What is drawn: the text, or one '*' per character for a password.
    std::string shown_;
    ScreenRect rect_;
    Icon icon_ = Icon::User;
    bool hasIcon_ = false;
    float textUnits_ = 24.0f;
};

// The fullscreen button (platform/Fullscreen.h) shows unless the page runs
// as a home-screen app, where the page can go fullscreen, and on an iPhone,
// where it cannot and a press shows fullscreenTip() instead.
bool fullscreenButtonShown();
const char* fullscreenTip();

} // namespace vibe::ui

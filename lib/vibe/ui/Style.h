#pragma once

#include <string_view>

#include "vibe/render/Renderer.h"
#include "vibe/render/Text.h"

// The look of plan 11's screens: dark navy panels with cut corners and gold
// frames on a warm dark background, gold gradient titles between diamond
// rules, card frames, and pointed gold and blue buttons. Everything is
// drawn from shapes (decision D1: SDL_Renderer, no shaders); the few icons
// come from assets/ui/. Sizes are in design units (Ui::u).
namespace vibe::ui {

class Ui;

namespace palette {
inline constexpr Color kBackTop{34, 20, 30, 255};
inline constexpr Color kBackBottom{10, 8, 14, 255};
inline constexpr Color kPanelTop{24, 28, 52, 246};
inline constexpr Color kPanelBottom{9, 11, 22, 246};
inline constexpr Color kGold{226, 182, 92, 255};
inline constexpr Color kGoldLight{255, 238, 176, 255};
inline constexpr Color kGoldDark{196, 128, 38, 255};
inline constexpr Color kGoldDeep{92, 58, 16, 255};
inline constexpr Color kSilver{92, 108, 150, 255};
inline constexpr Color kSilverLight{168, 186, 224, 255};
inline constexpr Color kCream{238, 228, 206, 255};
inline constexpr Color kMuted{196, 176, 136, 255};
inline constexpr Color kDim{128, 134, 158, 255};
inline constexpr Color kError{255, 128, 108, 255};
inline constexpr Color kGood{132, 226, 140, 255};
inline constexpr Color kBlue{120, 186, 255, 255};
inline constexpr Color kClear{0, 0, 0, 0};
} // namespace palette

// Gold, Blue, Dark: pixel-art buttons (ui::Frame). Round: a pixel-art disc
// with a gold rim (no label; Button::setIcon puts a glyph on it). Tile: a square card for an icon (no
// label) that lifts and glows on hover. Link: text only, underlined on hover.
enum class ButtonStyle { Gold, Blue, Dark, Round, Tile, Link };

namespace style {

// Shapes (canvas pixels). `out` gets 8 points: a rectangle with its corners
// cut by `cut`.
void chamfer(const ScreenRect& rect, float cut, ScreenVec* out);
// 6 points: a rectangle whose left and right ends are points `tip` deep.
void pointed(const ScreenRect& rect, float tip, ScreenVec* out);

// The screen behind the menus: a warm vignette and slow gold motes.
void background(Ui& ui);
// A framed panel: navy gradient, cut corners, a gold frame with a thin
// inner line, and a diamond on the top edge.
void panel(Ui& ui, const ScreenRect& rect);
// A strip across the top of a panel with a gold heading, `height` units.
void panelHeader(Ui& ui, const ScreenRect& panel, std::string_view text, float height);
// A card in a row of cards: navy with a silver frame, or gold-brown with a
// gold frame and a glow when `highlighted`.
void card(Ui& ui, const ScreenRect& rect, bool highlighted);
// A small diamond, optionally with a glow.
void diamond(Ui& ui, float cx, float cy, float radius, Color color, bool glow);
// The big gold title between two diamond rules. Returns the y below it.
float title(Ui& ui, std::string_view text, float cx, float y, float sizeUnits, Color top = palette::kGoldLight,
            Color bottom = palette::kGoldDark);
// A pointed label ("ROOM #12", "MASTER").
void pill(Ui& ui, const ScreenRect& rect, std::string_view text, bool gold, float sizeUnits = 18.0f);
// The pointed banner at the bottom of a screen with one line of text.
void banner(Ui& ui, const ScreenRect& rect, std::string_view text, Color color);
// A pointed button face (Button::draw uses it).
void buttonFace(Ui& ui, const ScreenRect& rect, ButtonStyle style, bool hover, bool pressed, bool enabled);
// Where pixel art `artW` x `artH` goes in `rect`: centered, at a whole
// number of canvas pixels per art pixel when that is within 15% of the fit
// (a fractional scale samples it unevenly and edges look rough).
ScreenRect pixelFit(const ScreenRect& rect, float artW, float artH);
// The round button's art in `rect` (buttons are laid out 42 x 46: 2 canvas
// pixels per art pixel at 1280 x 720).
ScreenRect roundSprite(const ScreenRect& rect);
// The pixel avatar ring with the user glyph on its grid (the player badge).
void avatar(Ui& ui, float cx, float cy, float radius, bool gold);

// ---- Lobby polish (2026-10-22): effects; the icons are art (Ui::icon) ----
// Seconds since start, for animations.
float now();
// 0..1 ease-out (cubic) and ease-out with a small overshoot.
float easeOut(float t);
float easeOutBack(float t);
// A small label in a dark pill under (cx, y), shown on hover.
void tooltip(Ui& ui, float cx, float y, std::string_view text);
// An on/off switch; `t` 0..1 is the knob's eased position.
void toggleSwitch(Ui& ui, const ScreenRect& rect, float t);
// A bright diagonal band sweeping across `rect` every `period` seconds.
void shimmer(Ui& ui, const ScreenRect& rect, float period, float offset = 0.0f);
// The pixel level hexagon (Icon::Level) with the level number on it.
void levelBadge(Ui& ui, float cx, float cy, float radius, std::string_view level);
// A thin rounded progress bar, `fraction` 0..1, with a moving glint.
void progressBar(Ui& ui, const ScreenRect& rect, float fraction, Color top, Color bottom);
// A four-pointed twinkle star.
void twinkle(Ui& ui, float cx, float cy, float radius, Color color);

} // namespace style
} // namespace vibe::ui

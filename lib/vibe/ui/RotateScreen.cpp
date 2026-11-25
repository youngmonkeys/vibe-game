#include "vibe/ui/RotateScreen.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

#include "vibe/core/AppInfo.h"
#include "vibe/ui/Style.h"
#include "vibe/ui/Ui.h"

namespace vibe::ui {

void RotateScreen::draw(Ui& ui) {
    style::background(ui);
    Renderer& r = ui.renderer();
    const ScreenRect& safe = ui.safe();
    // The landscape layout's scale is tiny in portrait; size this screen by
    // its short side instead (420 units across), in Ui's design units.
    const float px = std::min(safe.w, safe.h) / 420.0f;
    const float k = px / std::max(0.01f, ui.scale());
    const float cx = ui.cx();
    const float cy = safe.y + safe.h * 0.42f;

    // A phone outline turning from portrait to landscape, over and over.
    const float t = std::fmod(style::now(), 2.4f) / 2.4f;
    const float turn = std::clamp((t - 0.3f) / 0.4f, 0.0f, 1.0f);
    const float ease = turn * turn * (3.0f - 2.0f * turn);
    const float longSide = 150.0f * px;
    const float shortSide = 84.0f * px;
    const float w = shortSide + (longSide - shortSide) * ease;
    const float h = longSide + (shortSide - longSide) * ease;
    const ScreenRect phone{std::round(cx - w * 0.5f), std::round(cy - h * 0.5f), std::round(w), std::round(h)};
    r.fillScreenRect(phone, Color{10, 12, 26, 200});
    r.strokeScreenRect(phone, std::max(2.0f, 6.0f * px), ease >= 1.0f ? palette::kGoldLight : palette::kSilver);
    // The home button marks the bottom, then the right side.
    const float dot = 6.0f * px;
    if (ease < 0.5f) {
        r.fillScreenCircle(cx, phone.y + phone.h - 12.0f * px, dot, palette::kSilver);
    } else {
        r.fillScreenCircle(phone.x + phone.w - 12.0f * px, cy, dot, palette::kGoldLight);
    }

    style::title(ui, "ROTATE YOUR PHONE", cx, cy + longSide * 0.5f + 30.0f * px, 30.0f * k);
    std::string hint = app_info::kName;
    for (char& c : hint) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    hint += " PLAYS IN LANDSCAPE";
    ui.text(hint.c_str(), cx, cy + longSide * 0.5f + 80.0f * px,
            {.size = 16.0f * k, .color = palette::kMuted, .align = Align::Center});
    ui.mark("rotate.screen", safe);
}

} // namespace vibe::ui

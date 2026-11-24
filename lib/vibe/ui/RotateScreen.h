#pragma once

namespace vibe::ui {

class Ui;

// Plan 14: shown in place of every screen while a touch device is held in
// portrait (decision 2: landscape only). The game keeps running underneath;
// the match is on the server and does not pause.
class RotateScreen {
public:
    void draw(Ui& ui);
};

} // namespace vibe::ui

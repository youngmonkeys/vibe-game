#pragma once

#include <string>
#include <vector>

#include "vibe/core/Input.h"

namespace vibe::audio {

using Cue = int;

struct CueConfig {
    std::vector<std::string> names;
    Cue music = -1;
    Cue click = -1;
    Cue devTest = -1;
};

// Theme is music; the rest are effects. Every call runs on the main thread
// (decision D15), and every call is safe when init failed or never ran: the
// game is then silent, nothing else changes.
//
// Theme starts once the browser lets the page play audio (after the first
// click, key, or touch), if music is on, and loops until music is turned off.
// play(Track::Theme) only makes sure it is playing; it does not restart it.
// An effect restarts its own voice, as a Unity AudioSource.Play() does, so
// one track never stacks on itself. Effects that are asked for while the page
// cannot play audio yet, or while the tab is hidden, are skipped.
void play(Cue cue);

// The two settings toggles, saved with platform::saveSettings. Music off
// stops Theme; on starts it from the beginning. Sound effects off skips the
// five effects. Click plays on a change when sound effects are on.
void setMusicEnabled(bool enabled);
void setSoundEffectsEnabled(bool enabled);
bool musicEnabled();
bool soundEffectsEnabled();

// App only: SDL_AppInit, SDL_AppEvent (events no overlay owns), once per
// frame, and SDL_AppQuit. init reads the settings, opens the device paused,
// and loads the effects; a failure logs and leaves the game silent.
void init(const char* assetRoot, const CueConfig& cues);
void onEvent(const InputEvent& event);
void update();
void shutdown();

} // namespace vibe::audio

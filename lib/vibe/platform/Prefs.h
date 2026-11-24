#pragma once

namespace vibe::platform {

// The player's settings, the two Unity PlayerPrefs keys (`musicEnabled`,
// `soundEffectEnabled`). Gameplay reads this struct, not the file.
struct Settings {
    bool musicEnabled = true;
    bool soundEffectEnabled = true;
};

// Reads `settings.json` from SDL_GetPrefPath(VIBE_APP_ORG, VIBE_APP_NAME),
// which is on IndexedDB (IDBFS) in the browser. platform/web/idbfs.js loads it
// before main runs. A missing or unreadable file gives the defaults (both on)
// and is not an error; a bad file logs one warning.
Settings loadSettings();

// Writes the file with SDL_IOFromFile, then starts an IndexedDB sync. Returns
// false when the write failed (logged). When IndexedDB is not available (a
// private window can refuse it), the settings last for this page only.
bool saveSettings(const Settings& settings);

} // namespace vibe::platform

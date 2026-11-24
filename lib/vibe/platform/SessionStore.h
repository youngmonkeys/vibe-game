#pragma once

#include <string>

namespace vibe::platform {

// The login tokens in the tab's `sessionStorage`, under one key,
// `<VIBE_APP_ID>.session` (decision D15, plan 11). One copy per tab: a reload
// keeps it, closing the tab drops it, and two tabs never share it, so two tabs
// can be two players. Settings stay on IDBFS (Prefs.h); tokens never go there
// or to `localStorage`. The password is never stored.
//
// Any script on the page origin can read `sessionStorage`, which is why the
// release page loads no third-party script and sets a strict Content Security
// Policy (roadmap.md, release gate).
//
// Nothing here logs a token. Every call is safe when storage is refused (a
// private window, blocked site data): the tokens then last for this page only.
struct StoredTokens {
    std::string accessToken;
    std::string socketAccessToken;
};

// Writes both tokens. Returns false when the browser refused (logged).
bool saveTokens(const std::string& accessToken, const std::string& socketAccessToken);

// Reads both. Returns false, and leaves `tokens` empty, when the key is
// missing, unreadable, or lacks either token.
bool loadTokens(StoredTokens& tokens);

// Deletes the key: logout, and a server that rejected the token.
void clearTokens();

// Overwrites both strings with zeros and empties them.
void wipe(StoredTokens& tokens);

} // namespace vibe::platform

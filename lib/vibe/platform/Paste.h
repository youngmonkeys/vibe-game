#pragma once

#include <string>

namespace vibe::platform {

// Paste into the UI's text fields (plan 11). SDL 3.4.18 has no browser
// clipboard on Emscripten, so this listens for the page's `paste` event
// (Ctrl+V or Cmd+V; platform/web/shell.js lets that key reach the browser)
// and keeps the plain text until the frame takes it. The browser decides
// when paste is allowed; nothing here asks for clipboard permission.
void installPasteListener();

// Moves the text pasted since the last call into `out`. Returns false when
// there was none.
bool takePastedText(std::string& out);

} // namespace vibe::platform

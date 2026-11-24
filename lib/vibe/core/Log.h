#pragma once

namespace vibe {

// Routes SDL logging to stdout, and errors only to stderr. SDL's default on
// Emscripten writes every priority to stderr, which the page shows as
// console.error. The shell (platform/web/shell.js) prints stdout with
// console.log or console.warn, and keeps the last stderr line to show on the
// page if the module stops with a failure.
//
// Dev builds also turn on debug-level application logs.
void installLogOutput();

} // namespace vibe

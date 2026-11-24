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

#if defined(__GNUC__) || defined(__clang__)
#define VIBE_PRINTF_FORMAT(index) __attribute__((format(printf, index, index + 1)))
#else
#define VIBE_PRINTF_FORMAT(index)
#endif

void logDebug(const char* format, ...) VIBE_PRINTF_FORMAT(1);
void logInfo(const char* format, ...) VIBE_PRINTF_FORMAT(1);
void logWarn(const char* format, ...) VIBE_PRINTF_FORMAT(1);
void logError(const char* format, ...) VIBE_PRINTF_FORMAT(1);

} // namespace vibe

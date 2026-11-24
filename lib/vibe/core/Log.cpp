#include "vibe/core/Log.h"

#include <SDL3/SDL_log.h>

#include <cstdio>

#include "vibe/core/AppInfo.h"

#ifdef __ANDROID__
#include <android/log.h>
#endif

namespace vibe {
namespace {

[[maybe_unused]] const char* priorityPrefix(SDL_LogPriority priority) {
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE: return "TRACE: ";
    case SDL_LOG_PRIORITY_VERBOSE: return "VERBOSE: ";
    case SDL_LOG_PRIORITY_DEBUG: return "DEBUG: ";
    case SDL_LOG_PRIORITY_INFO: return "INFO: ";
    case SDL_LOG_PRIORITY_WARN: return "WARN: ";
    case SDL_LOG_PRIORITY_ERROR: return "ERROR: ";
    case SDL_LOG_PRIORITY_CRITICAL: return "CRITICAL: ";
    default: return "";
    }
}

void SDLCALL writeLog(void* /*userdata*/, int /*category*/, SDL_LogPriority priority, const char* message) {
#ifdef __ANDROID__
    int androidPriority = ANDROID_LOG_INFO;
    switch (priority) {
    case SDL_LOG_PRIORITY_TRACE:
    case SDL_LOG_PRIORITY_VERBOSE: androidPriority = ANDROID_LOG_VERBOSE; break;
    case SDL_LOG_PRIORITY_DEBUG: androidPriority = ANDROID_LOG_DEBUG; break;
    case SDL_LOG_PRIORITY_WARN: androidPriority = ANDROID_LOG_WARN; break;
    case SDL_LOG_PRIORITY_ERROR: androidPriority = ANDROID_LOG_ERROR; break;
    case SDL_LOG_PRIORITY_CRITICAL: androidPriority = ANDROID_LOG_FATAL; break;
    default: break;
    }
    __android_log_write(androidPriority, app_info::kName, message);
#else
    std::FILE* out = priority >= SDL_LOG_PRIORITY_ERROR ? stderr : stdout;
    std::fprintf(out, "%s%s\n", priorityPrefix(priority), message);
#endif
}

} // namespace

void installLogOutput() {
    SDL_SetLogOutputFunction(writeLog, nullptr);
#ifdef VIBE_DEV
    SDL_SetLogPriority(SDL_LOG_CATEGORY_APPLICATION, SDL_LOG_PRIORITY_DEBUG);
#endif
}

} // namespace vibe

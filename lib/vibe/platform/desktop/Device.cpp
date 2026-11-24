#include "vibe/platform/Device.h"

#include <SDL3/SDL_video.h>

#if defined(__APPLE__)
#include <TargetConditionals.h>
#include <mach/mach.h>
#include <malloc/malloc.h>
#elif defined(__ANDROID__) || defined(__linux__)
#include <malloc.h>
#include <unistd.h>

#include <cstdio>
#elif defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#endif

namespace vibe::platform {

bool isMobileUserAgent() {
#if TARGET_OS_IOS || defined(__ANDROID__)
    return true;
#else
    return false;
#endif
}

SafeInsets safeAreaInsets() {
#if TARGET_OS_IOS || defined(__ANDROID__)
    int count = 0;
    SDL_Window** windows = SDL_GetWindows(&count);
    SDL_Window* window = (windows && count > 0) ? windows[0] : nullptr;
    SDL_free(windows);
    SDL_Rect safe{};
    int w = 0;
    int h = 0;
    if (!window || !SDL_GetWindowSafeArea(window, &safe) || !SDL_GetWindowSize(window, &w, &h)) {
        return SafeInsets{};
    }
    return SafeInsets{static_cast<float>(safe.y), static_cast<float>(w - safe.x - safe.w),
                      static_cast<float>(h - safe.y - safe.h), static_cast<float>(safe.x)};
#else
    return SafeInsets{};
#endif
}

std::size_t wasmMemoryBytes() {
#if defined(__APPLE__)
    task_vm_info_data_t info{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO, reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS) {
        return static_cast<std::size_t>(info.phys_footprint);
    }
#elif defined(__ANDROID__) || defined(__linux__)
    FILE* statm = std::fopen("/proc/self/statm", "r");
    if (statm) {
        unsigned long size = 0;
        unsigned long resident = 0;
        const int read = std::fscanf(statm, "%lu %lu", &size, &resident);
        std::fclose(statm);
        if (read == 2) {
            return static_cast<std::size_t>(resident) * static_cast<std::size_t>(sysconf(_SC_PAGESIZE));
        }
    }
#elif defined(_WIN32)
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof counters;
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                             sizeof counters)) {
        return static_cast<std::size_t>(counters.PrivateUsage);
    }
#endif
    return 0;
}

std::size_t heapInUseBytes() {
#if defined(__APPLE__)
    return mstats().bytes_used;
#elif defined(__ANDROID__)
    return mallinfo().uordblks;
#elif defined(__linux__)
    return mallinfo2().uordblks;
#else
    return 0;
#endif
}

} // namespace vibe::platform

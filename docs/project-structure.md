# Cross-Platform 2D/3D Game Development Guide for AI Agents (C/C++)

This guide tells an AI coding agent which C/C++ libraries to use, how to structure the project, and how to build one codebase for **Windows, macOS, Linux, iOS, Android** **web browsers** (WebGL 2 / WebGPU) and the game consoles **PlayStation 5, Xbox Series X|S and Nintendo Switch / Switch 2**. Follow it unless the user explicitly asks otherwise.

---

## 1. Target platforms and native graphics APIs

| Platform | Native GPU API used | Toolchain | Package output |
|---|---|---|---|
| Windows 10/11 | Direct3D 12 (Vulkan fallback) | MSVC 2022 or clang-cl, Ninja or VS generator | `.exe` + DLLs, or installer |
| macOS 13+ | Metal | Xcode / Apple clang | `.app` bundle (universal `arm64;x86_64`) |
| Linux (x64, arm64) | Vulkan | GCC 12+ or Clang 15+ | binary, AppImage or Flatpak |
| iOS / iPadOS 15+ | Metal | Xcode, CMake Xcode generator | `.ipa` via Xcode |
| Android 7.0+ (API 24+) | Vulkan | Android NDK r26+, Gradle, CMake | `.apk` / `.aab` |
| Web (Chrome, Edge, Firefox, Safari) | WebGL 2 (WebGPU optional) | Emscripten (emsdk) | `.html` + `.js` + `.wasm` (+ `.data`) |
| PlayStation 5 | Sony proprietary API (NDA) | Sony SDK (Clang-based) | platform package |
| Xbox Series X\|S | Direct3D 12 (Xbox variant) | Microsoft GDK, MSVC | platform package |
| Nintendo Switch / Switch 2 | NVN (NDA); Vulkan is also available on Switch | Nintendo SDK (Clang-based) | platform package |

**Never use OpenGL / OpenGL ES directly in new code**, except in the WebGL 2 backend for the web (section 10). OpenGL is deprecated on Apple platforms, is being moved onto Vulkan on Android, and is not a first-class API on any console. All GPU access goes through SDL3 (section 2). Consoles have extra requirements; see section 9. The web has its own rules; see section 10.

---

## 2. Library stack

Pin every dependency to a **released tag** (never `main`/`master`). Before pinning, check the project's releases page for the latest stable tag.

### 2.1 Core (both 2D and 3D)

| Purpose | Library | Repository | Notes |
|---|---|---|---|
| Window, input, touch, gamepad, audio, filesystem, threads, app lifecycle | **SDL3** | github.com/libsdl-org/SDL | Use the SDL3 API only, never SDL2 (see 6.1). |
| GPU abstraction (Metal / D3D12 / Vulkan) | **SDL3 GPU API** (`SDL_gpu.h`) | part of SDL3 | Use it for 3D and for advanced 2D. |
| Shader cross-compilation | **SDL_shadercross** | github.com/libsdl-org/SDL_shadercross | Compile HLSL offline to SPIR-V, MSL and DXIL. |
| Image loading | **stb_image** or **SDL3_image** | github.com/nothings/stb, github.com/libsdl-org/SDL_image | |
| Fonts / text | **SDL3_ttf** (FreeType + HarfBuzz) or **stb_truetype** | github.com/libsdl-org/SDL_ttf | |
| Audio mixing | **miniaudio** or **SDL3 audio** | github.com/mackron/miniaudio | miniaudio adds 3D spatial sound and effects. |
| Debug UI / tools | **Dear ImGui** | github.com/ocornut/imgui | Use the `imgui_impl_sdl3` and `imgui_impl_sdlgpu3` backends. |
| ECS / game object model | **EnTT** (C++) or **flecs** (C/C++) | github.com/skypjack/entt, github.com/SanderMertens/flecs | Optional, for large entity counts. |
| Profiling | **Tracy** | github.com/wolfpld/tracy | Enable only in dev builds. |
| Scripting (optional) | **Lua 5.4** + **sol2** | github.com/ThePhD/sol2 | Interpreter only. Do not use LuaJIT's JIT: consoles and iOS forbid generating executable code at runtime. |
| Networking (optional) | **GameNetworkingSockets** or **ENet** | github.com/ValveSoftware/GameNetworkingSockets, github.com/lsalzman/enet | |

### 2.2 3D-specific

| Purpose | Library | Notes |
|---|---|---|
| Math | **GLM** (C++) or **cglm** (C) | Use the GLM headers with `GLM_FORCE_DEPTH_ZERO_TO_ONE` (see 5.4). |
| Physics | **Jolt Physics** | github.com/jrouwe/JoltPhysics. Its CMake project is in the `Build/` subdirectory. |
| Model loading | **cgltf** | Use glTF 2.0 (`.glb`) as the only runtime model format. |
| Mesh optimization | **meshoptimizer** | Run it in the asset pipeline, not at runtime. |
| Texture compression | **KTX-Software / Basis Universal** | Use `.ktx2` with BasisU, transcoded to ASTC on mobile/Apple and BC7 on desktop. |

### 2.3 2D-specific

| Purpose | Library | Notes |
|---|---|---|
| Simple 2D rendering | **SDL3 `SDL_Renderer`** | Sprites, rects, lines, textures. Works on every target with no shaders. Choose this first for simple 2D games. |
| Advanced 2D (shaders, lighting, huge sprite counts) | **SDL3 GPU API** + a custom sprite batcher | Use it when `SDL_Renderer` is too limited. |
| Physics | **Box2D v3** | github.com/erincatto/box2d. Has a C API. |
| Math | **GLM** or plain structs | |
| Tilemaps | **cute_tiled** (Tiled `.json` / `.tmj`) | github.com/RandyGaul/cute_headers |
| Sprite atlases | Pack offline (e.g. free-tex-packer, TexturePacker) and load a JSON atlas | |
| Skeletal 2D animation (optional) | **Spine C runtime** (needs a commercial license) or **DragonBones** | |

### 2.4 Choosing a renderer

```
Is the game 3D?
├── yes → SDL3 GPU API
└── no (2D)
    ├── needs custom shaders / post-processing / >50k sprites → SDL3 GPU API + sprite batcher
    └── otherwise → SDL_Renderer
```

On the web, `SDL_Renderer` works for 2D. 3D needs the WebGL 2 / WebGPU backend unless the pinned SDL version supports the SDL GPU API on the web (see 10.2).

---

## 3. Project layout

A game is split in two: the engine in `lib/` and the game itself in `src/`. The engine never includes game code; the game plugs into it through a few named hooks. This repository (Vibe Game) is the base project: it holds the engine and a small sample game. A real game either starts from a copy of the sample's `src/`, or lives in its own repository and points at this one.

### 3.1 This repository

```
vibe-game/
├── CMakeLists.txt          project() + include(cmake/VibeGame.cmake) + vibe_add_game(...)
├── CMakePresets.json       web-*, mac-*, ios-sim-*, android-*, windows-*, linux-*
├── cmake/
│   ├── VibeGame.cmake      platform detection, vibe_add_game, vibe_add_net_pure
│   ├── VibeHelpers.cmake   warnings, VIBE_DEV, sanitizers, sim float flags, Node tests
│   ├── Dependencies.cmake  pinned FetchContent: SDL3, SDL3_ttf, nlohmann/json, stb_vorbis, Dear ImGui (dev only)
│   ├── packaging/          MacBundle, IosBundle, AndroidApk, WindowsPackage, LinuxPackage, WebPage
│   └── toolchains/         mingw-w64-x86_64.cmake
├── lib/
│   └── vibe/               the engine, namespace vibe, included as "vibe/..."
│       ├── main.cpp        SDL main callbacks only; asks the game for vibe::createGame()
│       ├── core/           App (window, loop, fixed step), Game, Scene, HostConfig, Log, AppInfo, debug/ (ImGui)
│       ├── render/         Renderer (SDL_Renderer), Camera, Texture, Text (SDL3_ttf)
│       ├── audio/          music stream and effects by cue name
│       ├── platform/       ALL platform-specific code: device, fullscreen, paste, touch text, prefs, token store; web/, desktop/
│       ├── ui/             immediate-mode widgets; dev/ (dev builds only)
│       └── net/            ezylogin, game master, EzyFox session, decoders; transport/{web,tcp,apple,android,windows,linux}
├── platform/               default templates: web shell, Info.plist (macos, ios), AndroidManifest, engine Java, icons
├── src/                    the game (here: the sample, one scene and a guest login)
│   └── net/GameProtocol.h  the game's messages and protocol hooks
├── assets/                 runtime assets, packed as /assets (png, ogg, ttf, json)
├── config/configs.json     server addresses, packed as /config/configs.json
├── tools/                  asset build scripts (when a game adds them)
├── consoles/               PRIVATE git submodule (NDA code), absent in public checkouts
│   ├── ps5/
│   ├── xbox/
│   └── switch/
├── docs/                   this guide
└── README.md               how to write a game on the engine
```

### 3.2 A game in its own repository

```
my-game/
├── CMakeLists.txt          set(VIBE_GAME_DIR ...) + include(${VIBE_GAME_DIR}/cmake/VibeGame.cmake) + vibe_add_game(...)
├── CMakePresets.json       the same presets; the Windows toolchain path points into VIBE_GAME_DIR
├── src/                    game code only
│   ├── <Game>.cpp          defines vibe::createGame()
│   └── net/GameProtocol.h  GameHttpMessages, GameAppMessages + the protocol hooks (3.3)
├── platform/               only the files that replace an engine default (same relative path, e.g. web/icon-512.png)
├── assets/
├── config/
├── tests/                  game tests (game decoders through vibe_add_net_pure)
├── tools/
└── docs/
```

The engine is not copied into a game. A game points `VIBE_GAME_DIR` at this repository, so one fix in `lib/` reaches every game. The engine sources are compiled into the game's target by `vibe_add_game`, with the game's `src/` on the include path so `vibe/net/Messages.h` can include the game's `net/GameProtocol.h`.

### 3.3 The contract between engine and game

| Point | Engine | Game |
|---|---|---|
| Entry | `lib/vibe/main.cpp`, `vibe::App` | `std::unique_ptr<vibe::Game> vibe::createGame()` |
| Scenes | `vibe::Game::createInitialScene(const GameContext&)`, optional `audioCues`, `clearColor`, `onStart`, `update`, `flush`, `onQuit` | its own `vibe::Scene` subclasses |
| Identity | `VIBE_APP_NAME`, `VIBE_APP_ORG`, `VIBE_APP_ID` (window title, log tag, prefs path, session key, user agent) | `vibe_add_game(<target> NAME ORG ID BUNDLE_ID ...)` |
| Dev-only code | `lib/vibe/core/debug/`, `lib/vibe/ui/dev/`, `VIBE_DEV` | `DEV_DIRS` folders under `src/`, extra web exports in `WEB_DEV_EXPORTS` |
| Network messages | core messages + the game's lists joined into one flat `vibe::net::InboundMessage` variant | `net/GameProtocol.h` with `decodeGameCommand`, `decodeGameHttp`, `gameHttpStepName`, `isStreamMessage`, `coalesceStream` |
| HTTP | `Session::requestApi(step, method, path, body)` for steps from `vibe::net::gameHttpStep(0)` up | its step constants and decoders |
| Platform files | `platform/` defaults with `@VIBE_APP_NAME@` placeholders | overrides in its own `platform/` |

Rules:
- Do not put `#ifdef _WIN32 / __APPLE__ / __ANDROID__ / __EMSCRIPTEN__` (or any console macro) outside `lib/vibe/platform/` and `lib/vibe/net/transport/`. Game code has none.
- `lib/` never includes a game header, except the contract header `net/GameProtocol.h`. Something only one game needs stays in that game's `src/`.
- Game code in its own namespace reaches the engine through `vibe::`. A forward declaration of an engine type must be in `namespace vibe`, not in the game's namespace.
- The project must configure and build when `consoles/` is missing: guard it with `if(EXISTS ${CMAKE_SOURCE_DIR}/consoles/CMakeLists.txt)`.
- Gameplay code must not include SDL GPU headers directly; it goes through `vibe/render/`.

---

## 4. Build system (CMake + FetchContent)

### 4.1 A game's `CMakeLists.txt`

```cmake
cmake_minimum_required(VERSION 3.25)
project(MyGame VERSION 0.1.0 LANGUAGES C CXX)

set(VIBE_GAME_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vibe-game" CACHE PATH "Vibe Game base project")
include("${VIBE_GAME_DIR}/cmake/VibeGame.cmake")

vibe_add_game(MyGame
    NAME MyGame
    ORG mycompany
    ID mygame
    BUNDLE_ID com.mycompany.mygame
    DEV_DIRS dev
    WEB_DEV_EXPORTS _my_dev_hook)
```

Inside this repository `VIBE_GAME_DIR` is not needed: `include(cmake/VibeGame.cmake)`.

`vibe_add_game` globs `lib/vibe/**` (filtered per platform and for dev builds) and the game's `src/**`, creates the executable (an `.app` bundle on Apple, `libmain.so` on Android, an `.html` page on the web), sets the identity definitions, links SDL3, SDL3_ttf, nlohmann/json, stb_vorbis and, in dev builds, Dear ImGui, then includes the packaging module for the platform (`.app` with codesign, `.apk` without Gradle, a zip with the `.exe`, a tar.gz on Linux, the web page with its preload packages).

### 4.2 `cmake/Dependencies.cmake` template

This repository keeps the real file in `cmake/Dependencies.cmake`; games do not have their own. The template below is the general pattern for adding a library.

```cmake
include(FetchContent)

set(SDL_SHARED OFF CACHE BOOL "" FORCE)
set(SDL_STATIC ON CACHE BOOL "" FORCE)
FetchContent_Declare(SDL3
    GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
    GIT_TAG <latest-release-3.x-tag>
    GIT_SHALLOW TRUE)

FetchContent_Declare(glm
    GIT_REPOSITORY https://github.com/g-truc/glm.git
    GIT_TAG <latest-release-tag>
    GIT_SHALLOW TRUE)

set(TARGET_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(TARGET_HELLO_WORLD OFF CACHE BOOL "" FORCE)
set(TARGET_PERFORMANCE_TEST OFF CACHE BOOL "" FORCE)
set(TARGET_SAMPLES OFF CACHE BOOL "" FORCE)
set(TARGET_VIEWER OFF CACHE BOOL "" FORCE)
FetchContent_Declare(JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG <latest-release-tag>
    GIT_SHALLOW TRUE
    SOURCE_SUBDIR Build)

FetchContent_MakeAvailable(SDL3 glm JoltPhysics)
```

- Header-only libraries (cgltf, stb, miniaudio, cute_tiled) can go in `third_party/` or be fetched with `FetchContent_Populate` and added as include paths.
- Dear ImGui has no CMake project. Add `imgui*.cpp` and `backends/imgui_impl_sdl3.cpp` + `backends/imgui_impl_sdlgpu3.cpp` to a static library target.
- For 2D, replace `Jolt` with `box2d` (its CMake target is `box2d`; verify the target name in the pinned version).
- vcpkg (manifest mode, `vcpkg.json`) is an acceptable alternative to FetchContent. Do not mix both for the same library.

---

## 5. Code conventions for SDL3

### 5.1 Entry point: always use SDL main callbacks

Main callbacks are required for correct behavior on iOS and Android (and Emscripten). Never write your own `while (running)` loop in `main()`.

```cpp
#define SDL_MAIN_USE_CALLBACKS
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

struct AppState {
    SDL_Window *window = nullptr;
    SDL_GPUDevice *device = nullptr;
    Uint64 lastTicks = 0;
};

SDL_AppResult SDL_AppInit(void **appstate, int argc, char *argv[]) {
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMEPAD)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    auto *app = new AppState();
    *appstate = app;
    app->window = SDL_CreateWindow("Game", 1280, 720,
        SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!app->window) return SDL_APP_FAILURE;
    app->device = SDL_CreateGPUDevice(
        SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_MSL,
        false, nullptr);
    if (!app->device || !SDL_ClaimWindowForGPUDevice(app->device, app->window)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Error",
            "This device does not support the required GPU features.", nullptr);
        return SDL_APP_FAILURE;
    }
    app->lastTicks = SDL_GetTicksNS();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void *appstate, SDL_Event *event) {
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void *appstate) {
    auto *app = static_cast<AppState *>(appstate);
    Uint64 now = SDL_GetTicksNS();
    float dt = (now - app->lastTicks) / 1e9f;
    app->lastTicks = now;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void *appstate, SDL_AppResult result) {
    auto *app = static_cast<AppState *>(appstate);
    if (!app) return;
    SDL_ReleaseWindowFromGPUDevice(app->device, app->window);
    SDL_DestroyGPUDevice(app->device);
    SDL_DestroyWindow(app->window);
    delete app;
}
```

### 5.2 Frame structure with SDL GPU

```cpp
SDL_GPUCommandBuffer *cmd = SDL_AcquireGPUCommandBuffer(device);
SDL_GPUTexture *swapchain = nullptr;
Uint32 w = 0, h = 0;
if (!SDL_WaitAndAcquireGPUSwapchainTexture(cmd, window, &swapchain, &w, &h)) {
    SDL_SubmitGPUCommandBuffer(cmd);
    return SDL_APP_CONTINUE;
}
if (swapchain) {
    SDL_GPUColorTargetInfo color{};
    color.texture = swapchain;
    color.clear_color = {0.08f, 0.1f, 0.18f, 1.0f};
    color.load_op = SDL_GPU_LOADOP_CLEAR;
    color.store_op = SDL_GPU_STOREOP_STORE;
    SDL_GPURenderPass *pass = SDL_BeginGPURenderPass(cmd, &color, 1, &depthTargetInfo);
    SDL_EndGPURenderPass(pass);
}
SDL_SubmitGPUCommandBuffer(cmd);
```

- `swapchain` can be `nullptr` (window minimized or app in background). Skip rendering, but still submit the command buffer.
- Recreate the depth texture whenever `w`/`h` differ from its current size.
- Upload vertex/index/texture data through `SDL_GPUTransferBuffer` + a copy pass. Do this once at load time, not every frame.

### 5.3 Shaders

1. Write shaders **once in HLSL** under `shaders/`.
2. Compile them **offline** with `shadercross` into `.spv` (Vulkan), `.msl` (Metal) and `.dxil` (D3D12). Ship the compiled files in `assets/shaders/`.
3. At runtime, pick the format with `SDL_GetGPUShaderFormats(device)`:

```cpp
SDL_GPUShaderFormat formats = SDL_GetGPUShaderFormats(device);
const char *ext = nullptr;
const char *entry = "main";
SDL_GPUShaderFormat format = SDL_GPU_SHADERFORMAT_INVALID;
if (formats & SDL_GPU_SHADERFORMAT_SPIRV) { format = SDL_GPU_SHADERFORMAT_SPIRV; ext = "spv"; }
else if (formats & SDL_GPU_SHADERFORMAT_MSL) { format = SDL_GPU_SHADERFORMAT_MSL; ext = "msl"; entry = "main0"; }
else if (formats & SDL_GPU_SHADERFORMAT_DXIL) { format = SDL_GPU_SHADERFORMAT_DXIL; ext = "dxil"; }
```

4. Resource binding spaces required by SDL GPU (verify against the comments in `SDL_gpu.h` of the pinned version):

| Stage | Sampled textures / samplers, storage | Uniform buffers |
|---|---|---|
| Vertex | `space0` (SPIR-V `set 0`) | `space1` (SPIR-V `set 1`) |
| Fragment | `space2` (SPIR-V `set 2`) | `space3` (SPIR-V `set 3`) |

5. When creating a shader, the counts in `SDL_GPUShaderCreateInfo` (`num_samplers`, `num_uniform_buffers`, ...) must match the shader exactly.

### 5.4 Coordinate conventions

- SDL GPU clip space uses **depth range [0, 1]**. Define `GLM_FORCE_DEPTH_ZERO_TO_ONE` before including GLM, globally via `target_compile_definitions`.
- Use a right-handed world space, Y up, for 3D.
- Texture coordinate origin is top-left.
- For 2D, use pixel or world units with an orthographic projection. Keep sprite positions as `float`, and snap to whole pixels only when drawing pixel art.

### 5.5 2D with SDL_Renderer

```cpp
SDL_Renderer *renderer = SDL_CreateRenderer(window, nullptr);
SDL_SetRenderVSync(renderer, 1);
SDL_SetRenderLogicalPresentation(renderer, 320, 180, SDL_LOGICAL_PRESENTATION_LETTERBOX);
SDL_Texture *tex = SDL_CreateTextureFromSurface(renderer, surface);
SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

SDL_SetRenderDrawColor(renderer, 20, 26, 46, 255);
SDL_RenderClear(renderer);
SDL_FRect src{0, 0, 16, 16}, dst{x, y, 16, 16};
SDL_RenderTexture(renderer, tex, &src, &dst);
SDL_RenderPresent(renderer);
```

- Draw sprites from **one atlas texture** so they batch.
- Use `SDL_SetRenderLogicalPresentation` for a fixed virtual resolution on all screen sizes.

---

## 6. Rules the AI must follow

### 6.1 SDL3, not SDL2

SDL3 has a different API from SDL2. Common mistakes to avoid:

| SDL2 (wrong) | SDL3 (correct) |
|---|---|
| `SDL_Init` returns `0` on success | `SDL_Init` returns `bool` (`true` on success); same for most functions |
| `SDL_QUIT`, `SDL_KEYDOWN` | `SDL_EVENT_QUIT`, `SDL_EVENT_KEY_DOWN` |
| `event.key.keysym.sym` | `event.key.key` |
| `SDL_RWops`, `SDL_RWFromFile` | `SDL_IOStream`, `SDL_IOFromFile` |
| `SDL_RenderCopy` | `SDL_RenderTexture` (takes `SDL_FRect`) |
| `SDL_CreateWindow(title, x, y, w, h, flags)` | `SDL_CreateWindow(title, w, h, flags)` |
| `SDL_GameController*` | `SDL_Gamepad*` |
| `SDL_GetTicks()` returns `Uint32` | `SDL_GetTicks()` returns `Uint64`; use `SDL_GetTicksNS()` for precision |

If unsure about a signature, read the header in the fetched SDL source (`_deps/sdl3-src/include/SDL3/`) instead of guessing.

### 6.2 Files and assets

- Read assets only through `SDL_IOFromFile` / `SDL_LoadFile`, with paths relative to the asset root. On Android this reads from inside the APK (`assets/`); `fopen` does not work there.
- Asset root: `SDL_GetBasePath()` on desktop and iOS; relative paths on Android.
- Save games and settings go to `SDL_GetPrefPath("Company", "Game")`. Never write next to the executable.
- Use forward slashes `/` in all asset paths.

### 6.3 Mobile requirements

- Handle the app lifecycle in `SDL_AppEvent`: `SDL_EVENT_WILL_ENTER_BACKGROUND` (pause the game, save state, stop audio), `SDL_EVENT_DID_ENTER_FOREGROUND` (resume), `SDL_EVENT_TERMINATING`, and `SDL_EVENT_LOW_MEMORY`.
- Do not render while in the background.
- Support touch input (`SDL_EVENT_FINGER_*`) and on-screen controls. Do not assume a keyboard or mouse.
- Respect safe areas (notch, home indicator) with `SDL_GetWindowSafeArea`.
- Set orientation with `SDL_SetHint(SDL_HINT_ORIENTATIONS, "LandscapeLeft LandscapeRight")` before creating the window.
- Budget for mobile GPUs: compressed textures (ASTC), fewer draw calls, avoid large overdraw, cap at 60 FPS.
- `SDL_CreateGPUDevice` can fail on old Android devices without a usable Vulkan driver. Show an error message instead of crashing. For 2D games, fall back to `SDL_Renderer`.

### 6.4 Desktop requirements

- Handle window resize and high-DPI. Use `SDL_GetWindowSizeInPixels` for render targets.
- Support keyboard + mouse and gamepads (`SDL_EVENT_GAMEPAD_*`).
- Provide windowed and borderless fullscreen (`SDL_SetWindowFullscreen`).

### 6.5 Game loop and simulation

- Run physics at a **fixed timestep** (e.g. 60 Hz) with an accumulator. Render with interpolation.
- Clamp `dt` (e.g. max 0.1 s) to avoid a spiral of death after a stall or after resuming from the background.
- Jolt: create one `PhysicsSystem`, call `Update` with the fixed step, and use `JobSystemThreadPool` with `std::thread::hardware_concurrency() - 1` threads.
- Box2D v3: `b2World_Step(worldId, 1.0f / 60.0f, 4)`. Use meters as units (1 unit = 1 m), not pixels; convert when drawing.

### 6.6 General

- Use C++20 and RAII wrappers for SDL objects (e.g. a `unique_ptr` with a custom deleter).
- Do not use exceptions or RTTI in hot paths. Keep allocations out of the frame loop.
- Check every SDL call that can fail and log `SDL_GetError()`.
- Do not add a new dependency without checking that it builds for all public targets (Windows, macOS, Linux, iOS, Android, Web) and that its license allows console shipping (section 9.3).

---

## 7. Building for each platform

Use one build directory per platform/configuration. Output to `build/<platform>-<config>`. Web builds are described in 10.4.

### 7.1 Windows

```bat
cmake -S . -B build/windows-release -G "Visual Studio 17 2022" -A x64
cmake --build build/windows-release --config Release
```

Copy `assets/` next to `Game.exe` with a post-build step (`add_custom_command` + `copy_directory`).

### 7.2 macOS (universal binary)

```bash
cmake -S . -B build/macos-release -G Xcode -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64" -DCMAKE_OSX_DEPLOYMENT_TARGET=13.0
cmake --build build/macos-release --config Release
```

Copy assets into `Game.app/Contents/Resources` (add them as target sources with the `MACOSX_PACKAGE_LOCATION Resources` property). For distribution, sign with a Developer ID and notarize.

### 7.3 Linux

```bash
sudo apt install build-essential cmake ninja-build libvulkan-dev \
  libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxtst-dev \
  libwayland-dev libxkbcommon-dev wayland-protocols libdecor-0-dev \
  libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev libdbus-1-dev
cmake -S . -B build/linux-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/linux-release
```

Package as an AppImage or Flatpak for distribution. Check the exact dependency list in SDL's `docs/README-linux.md` for the pinned version.

### 7.4 iOS

```bash
cmake -S . -B build/ios -G Xcode -DCMAKE_SYSTEM_NAME=iOS \
  -DCMAKE_OSX_DEPLOYMENT_TARGET=15.0 \
  -DCMAKE_XCODE_ATTRIBUTE_DEVELOPMENT_TEAM=<TEAM_ID>
open build/ios/Game.xcodeproj
```

- `Info.plist` must include `UILaunchStoryboardName` and the supported orientations.
- Bundle the assets as resources (same as macOS).
- Test on a real device; the simulator's Metal support differs from real hardware.

### 7.5 Android

1. Copy `android-project/` from the SDL3 source tree into `platform/android/`.
2. Point `app/build.gradle` → `externalNativeBuild.cmake.path` at the root `CMakeLists.txt`.
3. The native library must be named **`main`** (`libmain.so`); the template in 4.1 already does this when `ANDROID` is set.
4. Set `minSdk 24` and limit `abiFilters` to `arm64-v8a` (add `x86_64` for emulators).
5. Copy or symlink `assets/` into `platform/android/app/src/main/assets/`.
6. Change `applicationId` and the `SDLActivity` subclass/package name for the game.

```bash
cd platform/android
./gradlew assembleDebug
./gradlew installDebug
```

Use `adb logcat -s SDL SDL/APP` to see `SDL_Log` output.

---

## 8. Asset pipeline

| Asset | Source format | Runtime format | Tool |
|---|---|---|---|
| 3D models | `.blend`, `.fbx` | `.glb` (glTF 2.0) | Blender export + `gltfpack` (meshoptimizer) |
| Textures | `.png` | `.ktx2` (BasisU) for 3D, `.png` atlas for 2D | `toktx` (KTX-Software), atlas packer |
| Shaders | `.hlsl` | `.spv`, `.msl`, `.dxil` | `shadercross` |
| Audio | `.wav` | `.ogg` (music), `.wav` (short SFX) | any encoder |
| Fonts | `.ttf` | `.ttf` or pre-baked SDF atlas | SDL3_ttf / msdfgen |
| Levels / tilemaps | Tiled `.tmx` | `.tmj` (JSON) | Tiled export |

Put the conversion scripts in `tools/` and run them from a CMake custom target (`cmake --build . --target assets`). Never convert assets at game runtime.

---

## 9. Game consoles (PlayStation 5, Xbox Series X|S, Nintendo Switch / Switch 2)

### 9.1 Access and confidentiality

- Console development requires a licensed developer account and dev kits: **PlayStation Partners** (Sony), **ID@Xbox / Xbox GDK** (Microsoft), **Nintendo Developer Portal** (Nintendo).
- Console SDKs, their documentation and the console ports of SDL3 are under **NDA**. Their APIs are not public.
- Rules for the AI:
    - **Do not guess console APIs.** Write console code only when the SDK headers or docs are present in the workspace. Otherwise stop and ask the user.
    - Put all console code inside `consoles/` (a private submodule). Never copy NDA code, headers, error codes or documentation text anywhere else: not into public files, commit messages, issues or chat outputs meant to be shared.
    - Public code (sections 3–8) must stay buildable without `consoles/`.

### 9.2 Graphics and SDL on each console

| Console | Native graphics API | Shader toolchain | SDL3 support |
|---|---|---|---|
| PlayStation 5 | Sony proprietary (NDA) | Sony shader compiler; HLSL-like shading language | Private SDL3 port for licensed developers |
| Xbox Series X\|S | Direct3D 12 (Xbox variant, via GDK) | DXC → DXIL | SDL3 has GDK support; the Xbox console parts are private |
| Nintendo Switch / Switch 2 | NVN (NDA); Vulkan also available on Switch | Nintendo shader compiler | Private SDL3 port for licensed developers |

- Ask the user, or check the platform holder's developer forums, whether the **SDL3 GPU API** backend is available for the target console in the SDK version used. **Do not assume it is.**
- If it is not available, implement a console backend of the engine's `vibe/render/` interface with the native API inside `consoles/<platform>/`. This is why gameplay code must never call SDL GPU directly (section 3).
- Keep shaders in HLSL. Add a console-specific compile step in `consoles/<platform>/` that uses the SDK's shader compiler.

### 9.3 Library compatibility

| Concern | Rule |
|---|---|
| License | Use only permissive licenses (MIT, zlib, BSD, Boost, Apache-2.0). **No GPL/LGPL**: their source and relinking obligations cannot be met on locked platforms. Use FreeType under its FTL license, not GPL. |
| Recommended stack | SDL3, GLM/cglm, Jolt, Box2D, cgltf, stb, Dear ImGui, EnTT/flecs and meshoptimizer are portable C/C++ and are used in shipped console games. |
| Needs a platform backend | miniaudio (write a custom backend or use SDL audio from the console port); Tracy (check console support in the pinned version); networking libraries (consoles require platform networking APIs and online services). |
| Forbidden on consoles | JIT compilation (LuaJIT JIT, runtime codegen); `dlopen`/plugins loaded from disk; `system()`/process spawning; writing outside platform-provided save/temp locations; relying on environment variables or command-line arguments. |
| Exceptions / RTTI | Available on modern console toolchains, but many studios disable them. Keep code compatible with `-fno-exceptions -fno-rtti` (return error codes / `std::expected`). |

### 9.4 Certification requirements to design for

Each platform holder has a certification checklist (Sony TRC, Xbox Requirements (XR), Nintendo guidelines). The exact rules are under NDA, but every console needs the following. Design for them from the start:

- **User accounts:** the game runs under a signed-in platform user and handles sign-out/user switching.
- **Save data:** save through the platform save API, per user. Handle storage-full and corrupted saves without crashing.
- **Suspend / resume / constrained mode:** pause the simulation, release or keep resources as the platform requires, and handle network loss on resume.
- **Controller disconnect:** pause and show a reconnect prompt; support controller reassignment.
- **Button names and glyphs:** show the platform's own buttons (Cross/Circle, A/B/X/Y, Nintendo's swapped A/B), including region differences in the confirm button.
- **Achievements / trophies, presence, activities:** go through the platform services interface.
- **Memory and performance:** fixed memory budget, no leaks, loading-time limits, stable frame rate.
- **Display:** TV-safe area, minimum text size readable on a TV, and Switch handheld vs docked resolution changes.
- **Localization and age ratings:** all user-facing text comes from string tables.

### 9.5 Architecture that keeps porting cheap

Define these interfaces in `lib/vibe/platform/` and implement them per platform (desktop/mobile in public code, consoles in `consoles/`):

```cpp
struct IPlatformServices {
    virtual ~IPlatformServices() = default;
    virtual bool signIn() = 0;
    virtual void unlockAchievement(std::string_view id) = 0;
    virtual void setPresence(std::string_view key) = 0;
};

struct ISaveStorage {
    virtual ~ISaveStorage() = default;
    virtual bool write(std::string_view slot, std::span<const std::byte> data) = 0;
    virtual std::optional<std::vector<std::byte>> read(std::string_view slot) = 0;
};

struct IInputGlyphs {
    virtual ~IInputGlyphs() = default;
    virtual std::string_view glyphFor(GameAction action) const = 0;
};

struct IRenderBackend {
    virtual ~IRenderBackend() = default;
    virtual void beginFrame() = 0;
    virtual void submit(const DrawList &list) = 0;
    virtual void endFrame() = 0;
};
```

- If Nintendo Switch is a target, use Switch-class hardware as the **minimum spec** for content budgets (memory, draw calls, texture sizes).
- Read input through game actions (`GameAction::Jump`), never raw keys or buttons, so remapping and platform glyphs work everywhere.

### 9.6 Building for consoles

- Each SDK ships its own compiler and a CMake toolchain file or Visual Studio integration. Configure with the toolchain file from the SDK:

```bash
cmake -S . -B build/<console>-release -DCMAKE_TOOLCHAIN_FILE=<path from the console SDK>
```

- Xbox builds use the Microsoft GDK with the Visual Studio generator.
- Console build machines are often offline. **Vendor dependencies** (git submodules or a local mirror) instead of downloading them with FetchContent at configure time. Set `FETCHCONTENT_FULLY_DISCONNECTED=ON` and point `FETCHCONTENT_SOURCE_DIR_<NAME>` at the vendored copies.
- Packaging, signing and submission use the platform holder's tools. Follow the SDK documentation; do not invent steps.

---

## 10. Web browsers (WebGL 2 / WebGPU via Emscripten)

### 10.1 Overview

| Item | Choice |
|---|---|
| Toolchain | Emscripten (emsdk). Pin the emsdk version used in CI. |
| Output | `Game.html` + `Game.js` + `Game.wasm` (+ `Game.data` for preloaded assets) |
| Window, input, audio | SDL3 (Emscripten backend) |
| 2D | `SDL_Renderer` (uses WebGL internally) |
| 3D / advanced 2D | A WebGL 2 implementation of `IRenderBackend` (9.5), using the OpenGL ES 3.0 API. Optionally a WebGPU implementation. |

- The SDL3 GPU API has had no web backend in stable releases. **Check the pinned SDL version.** If `SDL_CreateGPUDevice` works on Emscripten there, use it and skip the custom backend. Do not assume either way.
- WebGL 2 is the **only** place where an OpenGL-family API may be called directly, and only inside `lib/vibe/render/webgl/` (or `lib/vibe/render/webgpu/` for WebGPU).

### 10.2 WebGL 2 or WebGPU

| | WebGL 2 | WebGPU |
|---|---|---|
| Browser support | All modern desktop and mobile browsers | Newer; check current support (e.g. caniuse.com) before relying on it |
| API level | OpenGL ES 3.0 | Modern (close to Metal / Vulkan), has compute shaders |
| Emscripten | Built in (`-sMAX_WEBGL_VERSION=2`) | `--use-port=emdawnwebgpu` (Dawn bindings) |
| Use when | Default for every web build | Heavy rendering or compute; keep WebGL 2 as fallback |

### 10.3 Shaders on the web

- Keep HLSL as the only source. Compile HLSL → SPIR-V (`shadercross`), then:
    - WebGL 2: SPIR-V → GLSL ES 3.00 with SPIRV-Cross (`spirv-cross --es --version 300`).
    - WebGPU: SPIR-V → WGSL with Tint (Dawn) or naga.
- WebGL has no binding spaces. After linking a program, bind uniform blocks with `glGetUniformBlockIndex` + `glUniformBlockBinding`, and samplers with `glUniform1i`. Keep the binding numbers consistent with 5.3.
- Add the generated `.glsl` / `.wgsl` files to the asset pipeline (section 8).

### 10.4 CMake and build

Add to the root `CMakeLists.txt`:

```cmake
if(EMSCRIPTEN)
    set_target_properties(${GAME_TARGET} PROPERTIES SUFFIX ".html")
    target_link_options(${GAME_TARGET} PRIVATE
        -sMIN_WEBGL_VERSION=2
        -sMAX_WEBGL_VERSION=2
        -sALLOW_MEMORY_GROWTH=1
        -lidbfs.js
        --preload-file ${CMAKE_SOURCE_DIR}/assets@/assets
        --shell-file ${CMAKE_SOURCE_DIR}/platform/web/shell.html)
endif()
```

```bash
source <emsdk>/emsdk_env.sh
emcmake cmake -S . -B build/web-release -DCMAKE_BUILD_TYPE=Release
cmake --build build/web-release
emrun build/web-release/Game.html
```

- Always serve the files over HTTP (`emrun` or any web server). Opening `Game.html` with `file://` does not work.
- Configure the web server to serve `.wasm` as `application/wasm` and to compress `.wasm`, `.js` and `.data` with Brotli or gzip.

### 10.5 Rules for web builds

- **Main loop:** SDL main callbacks (5.1) are mandatory. A blocking `while` loop freezes the browser tab.
- **No blocking:** no `sleep`, busy-waiting, or synchronous network/file reads inside a frame.
- **Threads:** multithreading needs `SharedArrayBuffer`, which requires the server to send `Cross-Origin-Opener-Policy: same-origin` and `Cross-Origin-Embedder-Policy: require-corp`. Build with `-pthread` only when the host sends these headers. Otherwise build single-threaded: use `JobSystemSingleThreaded` for Jolt.
- **Assets:** `--preload-file` is fine for small games. For large games, keep the initial download small and fetch the rest on demand with `emscripten_fetch`. Show a loading progress bar.
- **Saves:** Emscripten's default in-memory filesystem is lost on page reload. In `lib/vibe/platform/web/` (with `platform/web/idbfs.js`), mount IDBFS at the save directory and call `FS.syncfs` after every write.
- **Audio:** browsers block audio until the user interacts with the page. Start or resume the audio device after the first click, key press or touch; show a "Click to start" screen.
- **Input:** mouse capture (`SDL_SetWindowRelativeMouseMode`) also needs a user gesture. Browser shortcuts (Ctrl+W, F5, ...) cannot be overridden. Support touch for mobile browsers.
- **Canvas size:** handle resize and `devicePixelRatio`; use `SDL_WINDOW_HIGH_PIXEL_DENSITY` and `SDL_GetWindowSizeInPixels` as on desktop.
- **Textures:** transcode KTX2/BasisU at load time to a format the browser supports (check the `WEBGL_compressed_texture_astc`, `_etc` and `_s3tc` extensions); fall back to RGBA8.
- **Memory:** wasm32 limits memory to 4 GB (2 GB by default with memory growth); mobile browsers often allow much less. Keep the memory budget at Switch level or lower.
- **Networking:** browsers have no raw TCP/UDP sockets, so GameNetworkingSockets and ENet do not work. Use WebSockets (`emscripten/websocket.h`) or WebRTC data channels, with a matching server endpoint.
- **Libraries:** Dear ImGui uses `imgui_impl_sdl3` + `imgui_impl_opengl3` (WebGL 2) or `imgui_impl_wgpu`. Disable Tracy. miniaudio supports Web Audio. Lua works. Exceptions are off by default; enable `-fwasm-exceptions` only if needed.

---

## 11. Verification checklist before finishing a task

- [ ] The code uses only SDL3 APIs (no SDL2 names, no direct OpenGL/Metal/D3D/Vulkan calls outside `lib/vibe/render/`).
- [ ] Shaders are written in HLSL, compiled for all three formats, and their binding spaces follow 5.3.
- [ ] All file access goes through `SDL_IOStream` / `SDL_GetPrefPath`.
- [ ] Lifecycle, touch and safe-area handling exist if the game targets mobile.
- [ ] Physics uses a fixed timestep.
- [ ] Platform-specific code is confined to `lib/vibe/platform/` and `lib/vibe/net/transport/`; the game's `src/` has none, and `lib/` includes no game header except `net/GameProtocol.h`.
- [ ] New dependencies are pinned to a release tag, build on Windows, macOS, Linux, iOS, Android and Web (Emscripten), and have a permissive license (MIT / zlib / BSD / Apache-2.0).
- [ ] No console SDK code, headers or NDA information was written outside `consoles/`.
- [ ] Saves, achievements, button glyphs and rendering go through the `lib/vibe/platform/` interfaces (9.5), so console ports do not touch gameplay code.
- [ ] Web builds do not block the main loop, start audio only after a user gesture, persist saves with IDBFS, and keep WebGL/WebGPU calls inside `lib/vibe/render/webgl/` or `lib/vibe/render/webgpu/`.
- [ ] Only the builds the user asked for were run, and their results are reported as they are.
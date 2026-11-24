# Vibe Game

Vibe Game is a base project for 2D games in C++20 on SDL3. One code base builds for the browser (WebAssembly), macOS, iOS, Android, Windows and Linux, and talks to an EzyPlatform / EzyFox game server out of the box. The engine lives in `lib/`. You write your game in `src/`.

## Layout

| Path | What |
|---|---|
| `lib/vibe/` | The engine (namespace `vibe`). Do not edit it for one game; changes here are shared by every game built on it. |
| `lib/vibe/main.cpp` | The SDL main callbacks. It asks your game for a `vibe::Game` through `vibe::createGame()`. |
| `lib/vibe/core/` | `App` (window, frame loop, fixed 40 Hz step), `Game`, `Scene`, `HostConfig`, `Log`, the ImGui dev overlay |
| `lib/vibe/render/` | `Renderer` (SDL_Renderer: rects, circles, sprites, nine-slice, gradients, debug text), `Camera`, `Texture`, `Text` (SDL3_ttf) |
| `lib/vibe/audio/` | Music streamed from Ogg Vorbis and sound effects, by cue names you list |
| `lib/vibe/platform/` | Device, fullscreen, paste, touch text input, settings file, token store, per platform |
| `lib/vibe/ui/` | Immediate-mode widgets (`Ui`, `Style`, drag scroll, rotate screen) |
| `lib/vibe/net/` | EzyPlatform login (ezylogin), game master (servers and instances), EzyFox session over WebSocket (web) or TCP / TCP+UDP with encryption (native) |
| `cmake/` | `VibeGame.cmake` with `vibe_add_game`, dependencies (pinned), packaging for each platform, the MinGW toolchain |
| `platform/` | Default web shell, Info.plist templates, AndroidManifest template, the engine's Android Java, default icons |
| `src/` | Your game. The sample here moves a square and logs in as a guest. |
| `config/configs.json` | Server addresses packed into the game as `/config/configs.json` |
| `assets/` | Your game's files, packed as `/assets` |

## Build

| Target | Configure and build | Output |
|---|---|---|
| macOS | `cmake --preset mac-debug && cmake --build build/mac-debug` | `build/mac-debug/VibeGame.app` |
| iOS Simulator | `cmake --preset ios-sim-debug && cmake --build build/ios-sim-debug` | `VibeGame.app` for the Simulator |
| Browser | `source <emsdk>/emsdk_env.sh`, then `cmake --preset web-debug && cmake --build build/web-debug` | `build/web-debug/VibeGame.html`, serve it over HTTP |
| Android | `ANDROID_NDK_HOME` and `ANDROID_HOME` set, then `cmake --preset android-debug && cmake --build build/android-debug` | `build/android-debug/VibeGame.apk` |
| Windows | `brew install mingw-w64` on a Mac, then `cmake --preset windows-debug && cmake --build build/windows-debug` | `build/windows-debug/VibeGame/VibeGame.exe` |
| Linux | `cmake --preset linux-debug && cmake --build build/linux-debug` (needs `libssl-dev`, `libcurl4-openssl-dev` and the SDL3 X11/Wayland dev packages) | `build/linux-debug/VibeGame/VibeGame` |

Debug and RelWithDebInfo builds define `VIBE_DEV` and link Dear ImGui; Release builds do not.

## Your game

`CMakeLists.txt` names the game:

```cmake
include(cmake/VibeGame.cmake)

vibe_add_game(MyGame
    NAME MyGame                     # window title, app name, output name
    ORG mycompany                   # settings folder (SDL_GetPrefPath)
    ID mygame                       # browser session storage key
    BUNDLE_ID com.mycompany.mygame  # macOS, iOS, Android id
    DEV_DIRS dev                    # folders under src/ compiled into dev builds only
    WEB_DEV_EXPORTS _my_dev_hook)   # extra C functions the dev web page calls
```

Every `.cpp` and `.mm` under `src/` is compiled. Put a file in `platform/` with the same path as one in the engine's `platform/` (for example `platform/web/icon-512.png` or `platform/web/shell.html`) to replace it for your game.

`src/` must provide two things:

1. `vibe::createGame()`, returning your `vibe::Game`. The game picks the first scene and can add cue names for audio, a clear color, and hooks that run every frame.

```cpp
class MyGame final : public vibe::Game {
public:
    std::unique_ptr<vibe::Scene> createInitialScene(const vibe::GameContext& context) override {
        return std::make_unique<MyScene>(context.renderer, context.config);
    }
};

std::unique_ptr<vibe::Game> vibe::createGame() {
    return std::make_unique<MyGame>();
}
```

2. `net/GameProtocol.h` and its hooks: the messages your server sends. The engine decodes the EzyFox frames and HTTP login itself, then hands your app commands and your HTTP answers to you.

| Hook | What it does |
|---|---|
| `GameHttpMessages`, `GameAppMessages` (in `net/GameProtocol.h`) | `vibe::net::MessageList<...>` of your structs; they join the engine's `InboundMessage` variant |
| `decodeGameCommand(command, payload, out, handled)` | Decodes one app command (`[appId, [command, data]]`) into `out`; set `handled = false` for one you do not know |
| `decodeGameHttp(step, status, body, elapsedMs, firstPage)` | Decodes the answer to a `Session::requestApi(step, ...)` call; steps start at `vibe::net::gameHttpStep(0)` |
| `gameHttpStepName(step)` | A short name for log lines |
| `isStreamMessage(message)`, `coalesceStream(queued, incoming)` | High-rate messages (positions) where only the newest counts; the inbound queue replaces an older one in place |

The sample's `src/net/GameProtocol.h` and `src/net/GameProtocol.cpp` are the empty versions. `vibe/net/DecodeSupport.h` has the JSON readers the engine's own decoders use (`readInt32`, `readName`, `field`, `failure`, ...).

## Scenes and the frame

`vibe::App` calls, in order, on every frame: `Game::update`, `Scene::frameStart` (drain the network here with `Session::update`), `Scene::fixedUpdate` at 40 Hz as many times as the clock needs, `Scene::render(alpha)` between the renderer's begin and end, `Scene::frameEnd` (send with `Session::flush`), `Game::flush`. Nothing may block the frame: no sleeps, no synchronous network calls. The browser build runs on one thread.

## Logging in

```cpp
vibe::net::Session session(config, listener);
session.quickPlay();                 // anonymous account, or session.login(user, password)
// listener.onLoggedIn()       -> session.requestGameServers()
// listener.onHttpResult(list) -> session.selectServer(id)
// listener.onSessionReady()   -> session.sendApp("command", "{...}")
// listener.onAppMessage(msg)  -> your decoded app commands
```

Release builds refuse `http://` and `ws://` server URLs; dev builds warn and continue.

## What is not generic yet

`lib/vibe/ui/` draws with the art and fonts of the game it came from (`assets/ui/art/*.png` and the fonts that `TextCache::init` is given). A game that uses `vibe::ui::Ui` has to ship those files in its own `assets/`. The sample does not use `Ui`, so it needs no assets.

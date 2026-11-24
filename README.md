# Vibe Game

Vibe Game is a base project for 2D games in C++20 on SDL3. One code base builds for the browser (WebAssembly), macOS, iOS, Android, Windows and Linux, and talks to an EzyPlatform / EzyFox game server out of the box. The engine lives in `lib/`. You write your game in `src/`.

## Layout

| Path | What |
|---|---|
| `lib/vibe/` | The engine (namespace `vibe`). Do not edit it for one game; changes here are shared by every game built on it. |
| `lib/vibe/main.cpp` | The SDL main callbacks. It asks your game for a `vibe::Game` through `vibe::createGame()`. |
| `lib/vibe/core/` | `App` (window, frame loop, fixed 40 Hz step), `Game`, `Scene`, `HostConfig`, `Log`, the ImGui dev overlay |
| `lib/vibe/render/` | `Renderer` (SDL_Renderer: rects, circles, sprites, nine-slice, gradients, debug text), `Camera`, `Texture`, `Text` (SDL3_ttf) |
| `lib/vibe/math/` | `Vec3`, `Vec4`, `Quat`, `Mat4`, `Transform`: the 3D math every part of the engine shares |
| `lib/vibe/node/` | `Node` (a tree of nodes, the Composite pattern), `NodeScene`, `SpriteNode` |
| `lib/vibe/audio/` | Music streamed from Ogg Vorbis and sound effects, by cue names you list |
| `lib/vibe/platform/` | Device, fullscreen, paste, touch text input, settings file, token store, per platform |
| `lib/vibe/ui/` | Immediate-mode widgets (`Ui`, `Style`, drag scroll, rotate screen) |
| `lib/vibe/net/` | EzyPlatform login (ezylogin), game master (servers and instances), EzyFox session over WebSocket (web) or TCP / TCP+UDP with encryption (native) |
| `cmake/` | `VibeGame.cmake` with `vibe_add_game`, dependencies (pinned), packaging for each platform, the MinGW toolchain |
| `platform/` | Default web shell, Info.plist templates, AndroidManifest template, the engine's Android Java, default icons |
| `src/` | Your game. The sample here moves a square and logs in as a guest. |
| `tests/node/` | Tests for the math, the camera and the node tree (`ctest` on macOS, Linux and the browser) |
| `config/configs.json` | Server addresses packed into the game as `/config/configs.json` |
| `assets/` | Your game's files, packed as `/assets` |
| `docs/project-structure.md` | The cross-platform guide: library stack, project layout, SDL3 conventions, per-platform builds, web rules, checklist |

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

## The world is 3D

World space is X right, Y up, Z toward the viewer, the same as the server. Positions are `vibe::Vec3`, rotations are `vibe::Quat`, and a `vibe::Transform` is position, rotation and scale. A 2D game keeps every z at 0 and turns things with `Quat::aroundZ(radians)`; nothing else changes.

`vibe::Camera` has a `position`, a `rotation` and a `projection`. The default is `Projection::Orthographic` with `orthoHalfHeight` 15, so the view is 30 world units high at any canvas size. `Projection::Perspective` uses `fovYRadians`, `nearPlane` and `farPlane`. The camera looks along its local -Z.

| Helper | What |
|---|---|
| `Camera::at(position)` | The default camera placed at a point |
| `pixelsPerUnit(camera, viewport)` | Canvas pixels per world unit for the orthographic camera |
| `worldToScreen(camera, viewport, x, y)` | A point on the z = 0 plane to canvas pixels; what `Renderer` uses |
| `projectToScreen(camera, viewport, point, out, &depth)` | Any 3D point through the full camera (rotation, either projection); false behind a perspective camera |
| `screenToWorldRay(camera, viewport, x, y, origin, direction)` | The ray under a canvas pixel, for picking |
| `camera.view()`, `camera.projectionMatrix(aspect)`, `camera.viewProjection(aspect)` | The matrices, for the SDL_GPU renderer to come |

`Renderer` still draws through SDL_Renderer: it draws the z = 0 plane with the orthographic camera's `position` and `orthoHalfHeight`, and ignores the camera's rotation and z. Real 3D drawing (meshes, depth, perspective) comes with the SDL_GPU renderer; the math and the camera are already in place for it.

## Nodes

`vibe::Node` is a tree: each node has a `Transform`, a parent and children, and every call on a node runs on its whole subtree (the Composite pattern). Use it for anything made of parts that move together: a character with its weapon and health bar, a panel holding buttons, a camera rig.

```cpp
class Hero final : public vibe::Node {
public:
    using Node::Node;

protected:
    void onFixedUpdate(float step) override { setPosition(position() + vibe::Vec3{2.0f * step, 0.0f, 0.0f}); }
};

class PlayScene final : public vibe::NodeScene {
public:
    PlayScene(vibe::Renderer& renderer, const vibe::Texture& swordTexture) : NodeScene(renderer) {
        auto& hero = root().emplaceChild<Hero>("hero");
        auto& sword = hero.emplaceChild<vibe::SpriteNode>("sword");
        sword.setTexture(&swordTexture);
        sword.setPosition(0.6f, 0.2f);
    }
};
```

| On a node | What |
|---|---|
| `emplaceChild<T>(args...)`, `addChild(std::unique_ptr<Node>)` | Adds a child; the parent owns it. Draw order is child order, later on top |
| `detach()`, `detachChild(child)` | Takes a node out of the tree and hands back ownership |
| `queueFree()` | Removes and destroys the node at the end of the frame (`NodeScene::frameEnd` calls `flushPending`); safe from inside its own update or event |
| `findChild(name)`, `findDescendant(name)`, `childAt(i)`, `childCount()` | Lookup |
| `setPosition`, `setRotation(Quat)`, `setRotationZ(radians)`, `setScale`, `setTransform` | The local transform, relative to the parent |
| `worldMatrix()`, `toWorld(local)`, `toLocal(world)` | World space |
| `setActive(false)` | No updates and no events for the subtree; it is still drawn |
| `setVisible(false)` | Not drawn and no events for the subtree |
| `snapInterpolation()` | Jump to the current transform this frame instead of sliding from the last step (after a teleport) |

The hooks a node overrides:

| Hook | When |
|---|---|
| `onEnterTree()`, `onExitTree()` | The node joined or left a running scene |
| `onFixedUpdate(step)` | Every 40 Hz step, parent before children |
| `onEvent(event)` | Children first, last child first (the one drawn on top); return true to stop the event there |
| `onRender(renderer, world)` | Every frame, parent before children; `world` is the node's world matrix between the last two steps, so motion is smooth at any frame rate |

`SpriteNode` draws a texture (or one atlas frame with `setSource` and `setPivot`) at its world position, rotation about Z and scale, `setUnitsPerPixel` world units per texture pixel (default 0.01).

`NodeScene` is a `vibe::Scene` that owns a root node and drives it. Override a scene hook and call the base one to keep the tree running. Adding, detaching and freeing nodes while the tree is being walked is safe. A scene that is not a `NodeScene` can still own a `Node` and call `enterTree`, `dispatchEvent`, `fixedUpdate`, `render` and `flushPending` itself.

## What is not generic yet

`lib/vibe/ui/` draws with the art and fonts of the game it came from (`assets/ui/art/*.png` and the fonts that `TextCache::init` is given). A game that uses `vibe::ui::Ui` has to ship those files in its own `assets/`. The sample does not use `Ui`, so it needs no assets.

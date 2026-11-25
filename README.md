# Vibe Game

Vibe Game is a base project for 2D games in C++20 on SDL3. One code base builds for the browser (WebAssembly), macOS, iOS, Android, Windows and Linux, and talks to an EzyPlatform / EzyFox game server out of the box. The engine lives in `lib/`. You write your game in `src/`.

## Layout

| Path | What |
|---|---|
| `lib/vibe/` | The engine (namespace `vibe`). Do not edit it for one game; changes here are shared by every game built on it. |
| `lib/vibe/sdl/` | The SDL backend: the SDL main callbacks (they ask your game for a `vibe::Game` through `vibe::createGame()`), `App` (frame loop, fixed 40 Hz step), the ImGui dev overlay, and the conversion from SDL events to `vibe::InputEvent`. The only engine headers that name SDL types live here. |
| `lib/vibe/core/` | `Game`, `Scene`, `SceneStack`, `Input` (events, keys, gamepads), `ActionMap`, `HostConfig`, `Log`, `Time` |
| `lib/vibe/render/` | `Renderer` (owns the window; SDL_Renderer: rects, circles, sprites, nine-slice, gradients, debug text), `Camera`, `Texture`, `Text` (SDL3_ttf) |
| `lib/vibe/math/` | `Vec3`, `Vec4`, `Quat`, `Mat4`, `Transform`: the 3D math every part of the engine shares |
| `lib/vibe/node/` | `Node` (a tree of nodes, the Composite pattern), `NodeScene`, `SpriteNode` |
| `lib/vibe/audio/` | Music streamed from Ogg Vorbis and sound effects, by cue names you list |
| `lib/vibe/platform/` | Device, fullscreen, paste, touch text input, settings file, token store, per platform |
| `lib/vibe/ui/` | Immediate-mode widgets (`Ui`, `Style`, drag scroll, rotate screen) |
| `lib/vibe/net/` | EzyPlatform login (ezylogin), game master (servers and instances), EzyFox session over WebSocket (web) or TCP / TCP+UDP with encryption (native); `Protocol` (decoders by command), `MessageDispatcher` (handlers by message type) |
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
    ZONE game                       # EzyFox zone the session logs in to
    APP mygame                      # EzyFox app the session opens
    DEV_DIRS dev                    # folders under src/ compiled into dev builds only
    WEB_DEV_EXPORTS _my_dev_hook)   # extra C functions the dev web page calls
```

`config/configs.json` holds only what changes per environment. `http` is required; the session reads the game servers and their instances (host, TCP port, UDP port, `websocketURL`) from the game master API at the same URL.

| Field | Required | Meaning |
|---|---|---|
| `http` | yes | Base URL of the web API, like `http://localhost:8080` or `https://mygame.example.com` |
| `webSocketUrl` (web), `tcpUrl` (native) | no | Dialed instead of the instance's URL, for a proxy, tunnel or NAT that the instance does not know about |
| `udpPort` | no | Used instead of the instance's UDP port |
| `udpUsage` | no | `true` adds the UDP channel on native builds; default `false` |
| `autoLogin` | no | Start with the tokens a previous login stored; default `false` |

Pick another file per build with `-DVIBE_CONFIG_FILE=<path>`.

Every `.cpp` and `.mm` under `src/` is compiled. Put a file in `platform/` with the same path as one in the engine's `platform/` (for example `platform/web/icon-512.png` or `platform/web/shell.html`) to replace it for your game.

`src/` must provide two things:

1. `vibe::createGame()`, returning your `vibe::Game`. The game picks the first scene and can add cue names for audio, a clear color, and hooks that run every frame.

```cpp
class MyGame final : public vibe::Game {
public:
    std::unique_ptr<vibe::Scene> createInitialScene(const vibe::GameContext& context) override {
        return std::make_unique<MyScene>(context.renderer, context.config, context.scenes);
    }
};

std::unique_ptr<vibe::Game> vibe::createGame() {
    return std::make_unique<MyGame>();
}
```

2. `net/GameProtocol.h`: the lists of messages your server sends, `GameHttpMessages` and `GameAppMessages` (`vibe::net::MessageList<...>` of your structs). They join the engine's messages in one flat `vibe::net::InboundMessage` variant. The sample's `src/net/GameProtocol.h` has empty lists.

## Network

The engine decodes the EzyFox frames and the ezylogin and game master HTTP answers itself. Your game adds three things, each one object per command, so a new command never touches a central `if` chain.

**A `vibe::net::Protocol`**, given to every `Session`, says how to decode your commands. Build it once and keep it alive as long as the sessions:

```cpp
const vibe::net::Protocol& protocol() {
    static const vibe::net::Protocol instance = [] {
        vibe::net::Protocol p;
        p.command<Moved>("moved", [](const Json& data, Moved& out) {
            const Json x = field(data, "x");
            return x && readFloat(*x, out.x) ? DecodeResult::ok() : DecodeResult::malformed("moved: bad x");
        });
        p.command("chat", decodeChat);  // DecodeResult(const Json&, InboundMessage&)
        p.http(kScoreStep, "score", [](const vibe::net::HttpAnswer& answer) { return decodeScore(answer); });
        p.stream<Moved>([](Moved& queued, const Moved& incoming) {
            if (queued.player != incoming.player) {
                return false;
            }
            queued = incoming;
            return true;
        });
        return p;
    }();
    return instance;
}
```

| Call | What |
|---|---|
| `command<T>(name, decode)` | App command `[appId, [name, data]]` into your struct `T`; a result other than `ok()` drops it |
| `command(name, decode)` | The same, with a decoder that fills the `InboundMessage` itself (one command, several types) |
| `http(step, name, decode)` | The answer to a `Session::request` with this step; `name` is for log lines. Steps start at `vibe::net::gameHttpStep(0)` |
| `stream<T>(merge)` | High-rate messages (positions) where only the newest counts: the inbound queue merges a newer one into the queued one when `merge` returns true |

A command with no decoder arrives as `OtherAppCommand`. `vibe::net::Json` is the engine's read-only view of a parsed JSON value (`is_object()`, `size()`, `[i]`, `member(key)`, `get<double>()`, range-for); a missing member is an empty view that tests false. The JSON library behind it stays inside `Json.cpp`. `vibe/net/DecodeSupport.h` has the readers the engine's own decoders use (`readInt32`, `readName`, `field`, `drop`, `failure`, ...).

**Handlers on `session.messages()`** receive the decoded messages by type: app commands once the session is ready, `SessionError` for an APP_REQUEST_ERROR, and every HTTP answer (`HttpGameServerList`, your HTTP structs, `HttpFailed` with its step). Each object subscribes to what it handles and keeps the returned `Subscription`; destroying it, or the `vibe::net::Subscriptions` list holding it, ends the subscription. Subscribing and unsubscribing from inside a handler is safe.

```cpp
class Hud {
public:
    explicit Hud(vibe::net::Session& session) {
        subscriptions_ += session.messages().on(this, &Hud::onScore);
        subscriptions_ += session.messages().on<Moved>([this](const Moved& moved) { lastX_ = moved.x; });
    }

private:
    void onScore(const Score& score);
    vibe::net::Subscriptions subscriptions_;
    float lastX_ = 0.0f;
};
```

`messages().otherwise(handler)` receives the messages no typed handler took. `dispatch` returns whether a typed handler ran.

**Request structs** for what your game sends:

```cpp
struct ChatRequest {
    static constexpr const char* kCommand = "chat";
    std::string_view message;
    bool encode(std::string& data) const;  // JSON text; false cancels the send
};

struct ScoreRequest {
    static constexpr vibe::net::HttpStep kStep = kScoreStep;
    static constexpr const char* kMethod = "GET";
    std::int64_t playerId = 0;
    std::string path() const { return "/api/v1/scores/" + std::to_string(playerId); }
};

session.send(ChatRequest{"hi"});          // [31,[appId,["chat",{...}]]]
session.request(ScoreRequest{42});        // the answer reaches messages()
session.inFlight<ScoreRequest>();
```

| Member | For | What |
|---|---|---|
| `kCommand` | `send` | The app command name |
| `encode(std::string&)` | `send`, optional | The data as JSON text; without it the data is `{}` |
| `kStep`, `kMethod` | `request` | The HTTP step and method |
| `kPath` or `path()` | `request` | Appended to the configs.json `http` URL; an empty path is not sent |
| `body()`, `firstPage` | `request`, optional | A JSON body; `firstPage` is handed to the decoder in `HttpAnswer` |

`Session::sendApp(command, json)`, `sendFrame(frame)` and `requestApi(step, method, path, body)` stay for a frame that does not fit a struct (a batch of inputs split over several frames).

## Input

Scenes, nodes and widgets get a `vibe::InputEvent`, never an SDL event, so the platform layer can change without touching game code.

| Field | Set for |
|---|---|
| `type` | `EventType::KeyDown`, `MouseButtonDown`, `FingerMotion`, `MouseWheel`, `TextInput`, `WindowHidden`, `Terminating`, `GamepadButtonDown`, ... |
| `key`, `physicalKey`, `repeat`, `mods` | Keys: `key` follows the keyboard layout, `physicalKey` is the key's place on a US layout (use it for WASD) |
| `button`, `x`, `y`, `leftHeld` | Mouse; `x`, `y` are window coordinates until `renderer.toCanvasCoordinates(event)` turns them into canvas pixels |
| `wheelX`, `wheelY` | Mouse wheel, with the pointer in `x`, `y` |
| `fingerId`, `x`, `y` | Touch; `x`, `y` are 0 to 1 until `toCanvasCoordinates` |
| `text` | Typed text, valid during the call only |
| `deviceId`, `gamepadButton` | Gamepads (and the keyboard id of a key, 0 for a posted key) |

`vibe::input` reads what is held now: `keyHeld(Key::W)`, `gamepadOpen(slot)`, `gamepadAxis(slot, GamepadAxis::LeftX)` (-1 to 1), `gamepadButtonHeld(slot, GamepadButton::South)`, `textInputActive()`. The engine opens up to `input::kMaxGamepads` gamepads as they connect and closes them when they go.

`vibe::input::ActionMap` (`vibe/core/ActionMap.h`) lets gameplay read actions instead of keys, so bindings can change without touching the code that reads them. An action is any enum of the game's own:

```cpp
enum class Action { Up, Down, Left, Right, Jump };

vibe::input::ActionMap actions;
actions.bindKey(Action::Up, vibe::Key::W).bindKey(Action::Up, vibe::Key::Up)
    .bindAxis(Action::Up, vibe::GamepadAxis::LeftY, -1.0f);
actions.bindKey(Action::Jump, vibe::Key::Space).bindButton(Action::Jump, vibe::GamepadButton::South);

actions.held(Action::Up);            // in fixedUpdate: a key, a button or a stick past kStickThreshold, on any gamepad
actions.pressed(Action::Jump, event); // in onEvent: a key down (not a repeat) or a button down bound to Jump
```

Keys bind by `physicalKey`. `unbind(action)` and `clear()` remove bindings, so a settings screen can rebind at run time.

`Game::update()` returns `vibe::AppResult::Continue`, `Success` (stop cleanly) or `Failure`. `vibe::logInfo`, `logWarn`, `logError`, `logDebug` (printf style) and `vibe::ticksMs()`, `ticksNs()` cover logging and time.

## Scenes and the frame

`vibe::App` calls, in order, on every frame: `Game::update`, `Scene::frameStart` (drain the network here with `Session::update`), `Scene::fixedUpdate` at 40 Hz as many times as the clock needs, `Scene::render(alpha)` between the renderer's begin and end, `Scene::frameEnd` (send with `Session::flush`), `Game::flush`. Nothing may block the frame: no sleeps, no synchronous network calls. The browser build runs on one thread.

The scenes live on a `vibe::SceneStack`, `context.scenes` in `GameContext`; the initial scene is its first entry. Keep the reference to change scenes later:

| Call | Effect |
|---|---|
| `scenes.replace<Next>(args...)` | the top scene leaves, `Next` enters (a menu to the game, the game to its result) |
| `scenes.push<Next>(args...)` | `Next` enters on top; the scenes under it stay (a pause menu, a dialog) |
| `scenes.pop()` | the top scene leaves |
| `scenes.clear()` | every scene leaves, top first |

Changes wait until the scene that asked has returned: App applies them after each event and at the start and end of each frame, so a scene may pop itself from `onEvent`. `onLeave` runs before the scene is destroyed.

With several scenes on the stack, only the top one gets `onEvent` and `fixedUpdate`, so the scenes under a pause menu stop. Every scene still gets `frameStart` (bottom first) and `frameEnd` (top first), so a scene under a menu keeps its network session alive. `render` starts at the highest scene whose `opaque()` is true (the default) and goes up; an overlay returns false to draw over the scene below. The sample's `PauseScene` (P) is such an overlay.

A scene can also own a `SceneStack` for scenes of its own and call `apply`, `onEvent`, `frameStart`, `fixedUpdate`, `render` and `frameEnd` itself, for example a shell scene that keeps the login and the network while matches come and go inside it.

## Logging in

```cpp
vibe::net::Session session(config, protocol(), listener);
session.quickPlay();                 // anonymous account, or session.login(user, password)
// listener.onLoggedIn()                    -> session.requestGameServers()
// messages().on<HttpGameServerList>(...)   -> session.selectServer(id)
// listener.onSessionReady()                -> session.send(JoinRequest{})
// messages().on<YourMessage>(...)          -> your decoded app commands
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

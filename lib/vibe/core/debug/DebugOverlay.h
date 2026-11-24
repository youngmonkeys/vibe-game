#pragma once

// Dev builds only (VIBE_DEV). CMake leaves src/app/debug/ out of a
// Release build, and Release does not link Dear ImGui.

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_video.h>

#include <string>
#include <vector>

struct SDL_Renderer;

namespace vibe::debug {

// The Dear ImGui context, with the SDL3 platform backend and the SDL_Renderer
// backend (decision D1: no imgui_impl_opengl3). App owns it. Scenes and the
// lobby draw ImGui windows between beginFrame and endFrame; no game rule lives
// in an ImGui callback.
class DebugOverlay {
public:
    bool init(SDL_Window* window, SDL_Renderer* renderer);
    void shutdown();

    // Feeds one event to ImGui. Returns true when ImGui owns it (a key while
    // a text field has focus, a click on an ImGui window); the scene then does
    // not see it.
    bool processEvent(const SDL_Event& event);

    // Inside Renderer::beginFrame / endFrame.
    void beginFrame();
    void endFrame();

    bool wantsKeyboard() const;

private:
    // Tab from one text field to the next has two gaps, seen in Chrome 155
    // and Firefox 158 when frames are slow (plan 07):
    // - ImGui activates the next field a few frames after Tab. A character in
    //   between finds no active field and is dropped.
    // - If ImGui stops wanting text input meanwhile, its SDL3 backend stops
    //   it. SDL then calls preventDefault on the next keydown, and the browser
    //   never fires the keypress that carries the character.
    // So after a Tab in a text field, keyboard events wait, and SDL text input
    // stays on, until another field is active and wants text (or
    // kTabHoldMaxFrames pass, when Tab went to a button). Then they replay in
    // order.
    static constexpr int kTabHoldMaxFrames = 10;
    struct HeldEvent {
        SDL_Event event;
        std::string text; // SDL frees a text event's string after dispatch
    };
    void releaseHeld();

    std::vector<HeldEvent> held_;
    bool holding_ = false;
    int holdFrames_ = 0;
    unsigned holdFromId_ = 0; // the ImGui item active at the Tab
    bool wantedTextLastFrame_ = false;
    unsigned activeIdLastFrame_ = 0;
    bool startedTextInput_ = false;
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    bool ready_ = false;
    bool inFrame_ = false;
};

} // namespace vibe::debug

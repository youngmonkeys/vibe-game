#include "vibe/core/debug/DebugOverlay.h"

#include <SDL3/SDL_keyboard.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_render.h>

#include <imgui.h>
#include <imgui_internal.h> // GetActiveID, for the Tab hold
#include <imgui_impl_sdl3.h>
#include <imgui_impl_sdlrenderer3.h>

namespace vibe::debug {

bool DebugOverlay::init(SDL_Window* window, SDL_Renderer* renderer) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    // No imgui.ini or log file: the page has no writable disk of its own (IDBFS
    // is plan 10's) and the layout is set in code.
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    ImGui::StyleColorsDark();
    // No style or font scaling for devicePixelRatio: ImGui works in CSS pixels
    // (the SDL window size) and endFrame scales the draw to canvas pixels.

    if (!ImGui_ImplSDL3_InitForSDLRenderer(window, renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "imgui: SDL3 backend init failed");
        ImGui::DestroyContext();
        return false;
    }
    if (!ImGui_ImplSDLRenderer3_Init(renderer)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "imgui: SDL_Renderer backend init failed");
        ImGui_ImplSDL3_Shutdown();
        ImGui::DestroyContext();
        return false;
    }
    window_ = window;
    renderer_ = renderer;
    ready_ = true;
    SDL_Log("imgui: %s, SDL3 and SDL_Renderer backends", IMGUI_VERSION);
    return true;
}

void DebugOverlay::shutdown() {
    if (!ready_) {
        return;
    }
    ImGui_ImplSDLRenderer3_Shutdown();
    ImGui_ImplSDL3_Shutdown();
    ImGui::DestroyContext();
    ready_ = false;
    window_ = nullptr;
    renderer_ = nullptr;
}

bool DebugOverlay::processEvent(const SDL_Event& event) {
    if (!ready_) {
        return false;
    }
    const bool keyboard = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP ||
                          event.type == SDL_EVENT_TEXT_INPUT;
    if (keyboard && holding_) {
        if (held_.size() < 64) {
            HeldEvent held{event, {}};
            if (event.type == SDL_EVENT_TEXT_INPUT && event.text.text) {
                held.text = event.text.text;
            }
            held_.push_back(std::move(held));
        }
        return true;
    }
    const ImGuiIO& io = ImGui::GetIO();
    const bool tabInTextField = event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_TAB && io.WantTextInput;
    ImGui_ImplSDL3_ProcessEvent(&event);
    if (tabInTextField) {
        holding_ = true;
        holdFrames_ = 0;
        holdFromId_ = activeIdLastFrame_;
    }
    switch (event.type) {
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
    case SDL_EVENT_TEXT_INPUT:
        return io.WantCaptureKeyboard;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_MOUSE_MOTION:
        return io.WantCaptureMouse;
    default:
        return false;
    }
}

void DebugOverlay::beginFrame() {
    if (!ready_) {
        return;
    }
    if (holding_) {
        ++holdFrames_;
        const bool nextFieldActive = wantedTextLastFrame_ && activeIdLastFrame_ != 0 && activeIdLastFrame_ != holdFromId_;
        if (nextFieldActive || holdFrames_ > kTabHoldMaxFrames) {
            releaseHeld();
        }
    }
    ImGui_ImplSDLRenderer3_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
    inFrame_ = true;
}

void DebugOverlay::endFrame() {
    if (!inFrame_) {
        return;
    }
    inFrame_ = false;
    // Render() ends the frame, which is when the SDL3 backend starts or stops
    // text input for ImGui; while holding, turn it back on after that.
    ImGui::Render();
    wantedTextLastFrame_ = ImGui::GetIO().WantTextInput;
    activeIdLastFrame_ = ImGui::GetActiveID();
    if (holding_ && !SDL_TextInputActive(window_)) {
        if (SDL_StartTextInput(window_)) {
            startedTextInput_ = true;
        } else {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "imgui: SDL_StartTextInput failed: %s", SDL_GetError());
        }
    }
    // The SDL_Renderer backend scales only its clip rectangles by the
    // framebuffer scale, not the geometry; the caller sets the render scale
    // (as ImGui's SDL3 + SDL_Renderer example does). Without it, at a
    // devicePixelRatio of 2 the widgets were drawn at half size and clipped
    // away (plan 07).
    const ImGuiIO& io = ImGui::GetIO();
    if (!SDL_SetRenderScale(renderer_, io.DisplayFramebufferScale.x, io.DisplayFramebufferScale.y)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "imgui: SDL_SetRenderScale failed: %s", SDL_GetError());
    }
    ImGui_ImplSDLRenderer3_RenderDrawData(ImGui::GetDrawData(), renderer_);
    SDL_SetRenderScale(renderer_, 1.0f, 1.0f);
}

void DebugOverlay::releaseHeld() {
    holding_ = false;
    holdFrames_ = 0;
    for (HeldEvent& held : held_) {
        if (held.event.type == SDL_EVENT_TEXT_INPUT) {
            held.event.text.text = held.text.c_str();
        }
        ImGui_ImplSDL3_ProcessEvent(&held.event);
    }
    held_.clear();
    // Hand text input back to ImGui's backend: off unless a field wants it.
    if (startedTextInput_ && !wantedTextLastFrame_) {
        SDL_StopTextInput(window_);
    }
    startedTextInput_ = false;
}

bool DebugOverlay::wantsKeyboard() const {
    return ready_ && ImGui::GetIO().WantCaptureKeyboard;
}

} // namespace vibe::debug

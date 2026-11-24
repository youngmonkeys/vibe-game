#pragma once

namespace vibe::platform {

// Browsers keep Web Audio suspended until the page gets a click, key, or
// touch. SDL 3.4.18 resumes its AudioContext by itself after that (it polls
// navigator.userActivation, research/gotchas.md), but until then it pulls
// bound streams into a buffer it throws away. src/audio/ keeps its device
// paused until this returns true.
//
// Reads the state of SDL's own AudioContext (Module.SDL3.audioContext). If
// that internal object is missing (a different SDL), it falls back to
// navigator.userActivation.hasBeenActive.
bool webAudioRunning();

// Plan 14: also resume SDL's AudioContext from inside each touch, click and
// key handler while it is not running. SDL's own resume runs from a timer
// after the first activation; iOS Safari may refuse a resume outside a
// gesture, and suspends ("interrupted") the context again after the app is
// sent to the background. Never creates a context of its own.
void installGestureResume();

} // namespace vibe::platform

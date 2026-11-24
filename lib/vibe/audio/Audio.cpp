#include "vibe/audio/Audio.h"

#include <SDL3/SDL_audio.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>
#include <SDL3/SDL_timer.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cmath>
#include <cstdlib>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

#include "vibe/audio/MusicStream.h"
#include "vibe/audio/StbVorbis.h"
#include "vibe/platform/Device.h"
#include "vibe/platform/Prefs.h"
#include "vibe/platform/WebAudio.h"

namespace vibe::audio {
namespace {

using Json = nlohmann::json;

// One effect, converted once to the device format at load.
struct Effect {
    Uint8* samples = nullptr; // SDL_ConvertAudioSamples; SDL_free
    int bytes = 0;
    int delayBytes = 0; // Unity's delayInSeconds, as leading silence
    float volume = 1.0f;
    SDL_AudioStream* voice = nullptr;
    unsigned played = 0;
    unsigned skipped = 0;
};

struct TrackInfo {
    std::string file;
    float volume = 1.0f;
    float delaySeconds = 0.0f;
    bool found = false;
};

class System {
public:
    void init(const char* assetRoot, const CueConfig& cues);
    void shutdown();
    void update();
    void onEvent(const SDL_Event& event);
    void play(Cue cue);
    void setMusic(bool enabled);
    void setEffects(bool enabled);
    bool music() const { return settings_.musicEnabled; }
    bool effects() const { return settings_.soundEffectEnabled; }

private:
    static bool SDLCALL watchEvent(void* userdata, SDL_Event* event);
    bool readManifest(const std::string& path, std::vector<TrackInfo>& tracks);
    void loadEffect(Cue cue, const TrackInfo& info);
    bool validCue(Cue cue) const { return cue >= 0 && static_cast<std::size_t>(cue) < cues_.names.size(); }
    const char* cueName(Cue cue) const { return validCue(cue) ? cues_.names[static_cast<std::size_t>(cue)].c_str() : "?"; }
    bool canPlay() const { return device_ != 0 && unlocked_ && !hidden_; }
    void applyPause();
    void startMusic();
    void save();
#ifdef VIBE_DEV
    void logStats(std::uint64_t nowNs);
#endif

    platform::Settings settings_;
    CueConfig cues_;
    SDL_AudioDeviceID device_ = 0;
    SDL_AudioSpec spec_{};
    bool subsystem_ = false;
    bool watching_ = false;
    bool unlocked_ = false; // the browser runs Web Audio (after a gesture)
    bool hidden_ = false;   // between WINDOW_HIDDEN and WINDOW_SHOWN
    bool paused_ = true;
    std::uint64_t initNs_ = 0;
    std::string musicPath_;
    float musicVolume_ = 1.0f;
    bool musicBroken_ = false;
    MusicStream music_;
    std::vector<Effect> effects_;
    std::vector<Uint8> silence_; // for effect delays; never written after init
#ifdef VIBE_DEV
    std::uint64_t statsNs_ = 0;
#endif
};

System gSystem;

bool System::readManifest(const std::string& path, std::vector<TrackInfo>& tracks) {
    size_t size = 0;
    void* data = SDL_LoadFile(path.c_str(), &size);
    if (!data) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: cannot read %s: %s", path.c_str(), SDL_GetError());
        return false;
    }
    const char* text = static_cast<const char*>(data);
    const Json root = Json::parse(text, text + size, nullptr, false);
    SDL_free(data);
    if (!root.is_object()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s is not JSON", path.c_str());
        return false;
    }
    const auto audio = root.find("audio");
    if (audio == root.end() || !audio->is_object()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s has no audio object", path.c_str());
        return false;
    }
    const auto list = audio->find("tracks");
    if (list == audio->end() || !list->is_object()) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s has no audio.tracks object", path.c_str());
        return false;
    }
    for (std::size_t i = 0; i < tracks.size(); ++i) {
        const char* name = cues_.names[i].c_str();
        const auto entry = list->find(cues_.names[i]);
        if (entry == list->end() || !entry->is_object()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: manifest has no track %s", name);
            continue;
        }
        const auto file = entry->find("file");
        if (file == entry->end() || !file->is_string()) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: track %s has no file", name);
            continue;
        }
        TrackInfo& info = tracks[i];
        info.file = file->get<std::string>();
        if (const auto v = entry->find("volume"); v != entry->end() && v->is_number()) {
            info.volume = std::clamp(v->get<float>(), 0.0f, 1.0f);
        }
        if (const auto d = entry->find("delaySeconds"); d != entry->end() && d->is_number()) {
            info.delaySeconds = std::clamp(d->get<float>(), 0.0f, 2.0f);
        }
        info.found = true;
    }
    return true;
}

void System::loadEffect(Cue cue, const TrackInfo& info) {
    const char* name = cueName(cue);
    size_t fileSize = 0;
    void* file = SDL_LoadFile(info.file.c_str(), &fileSize);
    if (!file) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s: cannot read %s: %s", name, info.file.c_str(),
                    SDL_GetError());
        return;
    }
    int channels = 0;
    int rate = 0;
    short* pcm = nullptr;
    const int frames = fileSize > static_cast<size_t>(INT_MAX)
                           ? -1
                           : stb_vorbis_decode_memory(static_cast<const unsigned char*>(file),
                                                      static_cast<int>(fileSize), &channels, &rate, &pcm);
    SDL_free(file);
    if (frames <= 0 || !pcm || channels <= 0 || rate <= 0 || frames > INT_MAX / channels / 2) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s: %s is not a playable Ogg Vorbis file", name,
                    info.file.c_str());
        std::free(pcm);
        return;
    }
    const SDL_AudioSpec source{SDL_AUDIO_S16, channels, rate};
    Effect& effect = effects_[static_cast<std::size_t>(cue)];
    const bool converted = SDL_ConvertAudioSamples(&source, reinterpret_cast<const Uint8*>(pcm),
                                                   frames * channels * static_cast<int>(sizeof(short)), &spec_,
                                                   &effect.samples, &effect.bytes);
    std::free(pcm);
    if (!converted) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s: converting %s failed: %s", name, info.file.c_str(),
                    SDL_GetError());
        effect.samples = nullptr;
        effect.bytes = 0;
        return;
    }
    effect.voice = SDL_CreateAudioStream(&spec_, &spec_);
    if (!effect.voice || !SDL_BindAudioStream(device_, effect.voice)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s: no voice: %s", name, SDL_GetError());
        SDL_DestroyAudioStream(effect.voice);
        effect.voice = nullptr;
        return;
    }
    effect.volume = info.volume;
    SDL_SetAudioStreamGain(effect.voice, info.volume);
    const int frameBytes = SDL_AUDIO_FRAMESIZE(spec_);
    effect.delayBytes = static_cast<int>(std::lround(info.delaySeconds * static_cast<float>(spec_.freq))) * frameBytes;
    if (effect.delayBytes > static_cast<int>(silence_.size())) {
        silence_.assign(static_cast<std::size_t>(effect.delayBytes), 0);
    }
}

void System::init(const char* assetRoot, const CueConfig& cues) {
    initNs_ = SDL_GetTicksNS();
    cues_ = cues;
    effects_.assign(cues_.names.size(), Effect{});
    settings_ = platform::loadSettings();
    if (!SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_InitSubSystem(AUDIO) failed: %s; no sound",
                    SDL_GetError());
        return;
    }
    subsystem_ = true;
    device_ = SDL_OpenAudioDevice(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, nullptr);
    if (!device_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_OpenAudioDevice failed: %s; no sound", SDL_GetError());
        return;
    }
    // Paused until the browser runs Web Audio. Before that, SDL's Emscripten
    // backend pulls bound streams into a buffer it discards, so the start of
    // Theme would be lost unheard.
    SDL_PauseAudioDevice(device_);
    paused_ = true;
    int frames = 0;
    if (!SDL_GetAudioDeviceFormat(device_, &spec_, &frames)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_GetAudioDeviceFormat failed: %s; no sound",
                    SDL_GetError());
        SDL_CloseAudioDevice(device_);
        device_ = 0;
        return;
    }
    // Tab visibility has to reach the device at once. SDL queues the window
    // events, and Chrome runs no frame in a hidden tab, so SDL_AppEvent would
    // only see HIDDEN when the tab comes back (research/gotchas.md, Web). An
    // event watch runs when SDL posts the event, inside visibilitychange.
    watching_ = SDL_AddEventWatch(watchEvent, this);
    if (!watching_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_AddEventWatch failed: %s", SDL_GetError());
    }

    const std::string root = assetRoot;
    std::vector<TrackInfo> tracks(cues_.names.size());
    if (!tracks.empty() && readManifest(root + "manifest.json", tracks)) {
        for (std::size_t i = 0; i < tracks.size(); ++i) {
            TrackInfo& info = tracks[i];
            if (!info.found) {
                continue;
            }
            info.file = root + info.file;
            if (static_cast<Cue>(i) == cues_.music) {
                musicPath_ = info.file;
                musicVolume_ = info.volume;
            } else {
                loadEffect(static_cast<Cue>(i), info);
            }
        }
    }
    std::size_t effectBytes = 0;
    int loaded = 0;
    for (const Effect& effect : effects_) {
        effectBytes += static_cast<std::size_t>(effect.bytes);
        loaded += effect.voice ? 1 : 0;
    }
    SDL_Log("audio: device %d Hz, %d channels, %d frames; %d effects in device format (%.1f MB), music %s; "
            "music %s, sound effects %s; waiting for the first click or key",
            spec_.freq, spec_.channels, frames, loaded, static_cast<double>(effectBytes) / 1e6,
            musicPath_.empty() ? "missing" : musicPath_.c_str(), settings_.musicEnabled ? "on" : "off",
            settings_.soundEffectEnabled ? "on" : "off");
}

void System::shutdown() {
    if (watching_) {
        SDL_RemoveEventWatch(watchEvent, this);
        watching_ = false;
    }
    music_.close();
    for (Effect& effect : effects_) {
        SDL_DestroyAudioStream(effect.voice);
        SDL_free(effect.samples);
        effect = Effect{};
    }
    silence_.clear();
    if (device_) {
        SDL_CloseAudioDevice(device_);
        device_ = 0;
    }
    if (subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
        subsystem_ = false;
    }
    unlocked_ = false;
}

bool SDLCALL System::watchEvent(void* userdata, SDL_Event* event) {
    auto* self = static_cast<System*>(userdata);
    if (event->type == SDL_EVENT_WINDOW_HIDDEN && !self->hidden_) {
        self->hidden_ = true;
        self->applyPause();
    } else if (event->type == SDL_EVENT_WINDOW_SHOWN && self->hidden_) {
        // SDL also sends SHOWN when the window is created; only a return
        // from a hidden tab counts.
        self->hidden_ = false;
        self->applyPause();
    }
    return true;
}

void System::applyPause() {
    if (!device_) {
        return;
    }
    const bool pause = !unlocked_ || hidden_;
    if (pause == paused_) {
        return;
    }
    if (!(pause ? SDL_PauseAudioDevice(device_) : SDL_ResumeAudioDevice(device_))) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s failed: %s",
                    pause ? "SDL_PauseAudioDevice" : "SDL_ResumeAudioDevice", SDL_GetError());
        return;
    }
    paused_ = pause;
    SDL_Log("audio: device %s%s", pause ? "paused" : "playing", hidden_ ? " (tab hidden)" : "");
}

void System::update() {
    if (!device_) {
        return;
    }
    const std::uint64_t now = SDL_GetTicksNS();
    if (!unlocked_ && platform::webAudioRunning()) {
        unlocked_ = true;
        SDL_Log("audio: Web Audio running %.2f s after start", static_cast<double>(now - initNs_) / 1e9);
        applyPause();
        if (settings_.musicEnabled) {
            startMusic();
        }
    }
#ifdef VIBE_DEV
    logStats(now);
#endif
}

void System::startMusic() {
    if (!device_ || musicBroken_ || musicPath_.empty()) {
        return;
    }
    if (!music_.isOpen()) {
        // Opened on the first play, so a player with music off never holds
        // the compressed file in the heap.
        if (!music_.open(musicPath_.c_str(), spec_, musicVolume_) || !SDL_BindAudioStream(device_, music_.stream())) {
            SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: no music: %s", SDL_GetError());
            music_.close();
            musicBroken_ = true;
            return;
        }
    }
    music_.start();
    SDL_Log("audio: %s playing", cueName(cues_.music));
}

void System::play(Cue cue) {
    if (!validCue(cue)) {
        return;
    }
    if (cue == cues_.music) {
        if (settings_.musicEnabled && unlocked_ && !music_.playing()) {
            startMusic();
        }
        return;
    }
    Effect& effect = effects_[static_cast<std::size_t>(cue)];
    if (!effect.voice || !settings_.soundEffectEnabled || !canPlay()) {
        ++effect.skipped;
        return;
    }
    // Restart, like AudioSource.Play(): drop what is left of the last play.
    // Both puts reference memory that lives until shutdown, so nothing is
    // copied per play.
    SDL_ClearAudioStream(effect.voice);
    if (effect.delayBytes > 0) {
        SDL_PutAudioStreamDataNoCopy(effect.voice, silence_.data(), effect.delayBytes, nullptr, nullptr);
    }
    if (!SDL_PutAudioStreamDataNoCopy(effect.voice, effect.samples, effect.bytes, nullptr, nullptr)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s: SDL_PutAudioStreamDataNoCopy failed: %s",
                    cueName(cue), SDL_GetError());
        return;
    }
    ++effect.played;
#ifdef VIBE_DEV
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION, "audio: play %s (%u)", cueName(cue), effect.played);
#endif
}

void System::save() {
    platform::saveSettings(settings_);
}

void System::setMusic(bool enabled) {
    if (enabled == settings_.musicEnabled) {
        return;
    }
    settings_.musicEnabled = enabled;
    save();
    if (enabled) {
        if (unlocked_) {
            startMusic();
        }
    } else {
        music_.stop();
        SDL_Log("audio: %s stopped (music off)", cueName(cues_.music));
    }
    play(cues_.click);
}

void System::setEffects(bool enabled) {
    if (enabled == settings_.soundEffectEnabled) {
        return;
    }
    settings_.soundEffectEnabled = enabled;
    save();
    play(cues_.click);
}

void System::onEvent([[maybe_unused]] const SDL_Event& event) {
#ifdef VIBE_DEV
    // Dev keys: M music, N sound effects, B one Attack (no swing), L jump to
    // 3 s before the end of Theme. Plan 11's lobby has the two toggles; the
    // keys stay for tests, but not while a text field takes typing.
    if (event.type != SDL_EVENT_KEY_DOWN || event.key.repeat) {
        return;
    }
    if (SDL_Window* window = SDL_GetKeyboardFocus(); window && SDL_TextInputActive(window)) {
        return;
    }
    switch (event.key.scancode) {
    case SDL_SCANCODE_M:
        setMusic(!settings_.musicEnabled);
        break;
    case SDL_SCANCODE_N:
        setEffects(!settings_.soundEffectEnabled);
        break;
    case SDL_SCANCODE_B:
        play(cues_.devTest);
        break;
    case SDL_SCANCODE_L:
        music_.seekBeforeEnd(3.0f);
        SDL_Log("audio: %s at %.2f s (dev key L)", cueName(cues_.music), static_cast<double>(music_.decodedSeconds()));
        break;
    default:
        break;
    }
#endif
}

#ifdef VIBE_DEV
void System::logStats(std::uint64_t nowNs) {
    constexpr std::uint64_t kIntervalNs = 2'000'000'000;
    if (nowNs - statsNs_ < kIntervalNs) {
        return;
    }
    statsNs_ = nowNs;
    const std::size_t heapBytes = platform::heapInUseBytes();
    unsigned played = 0;
    unsigned skipped = 0;
    for (const Effect& effect : effects_) {
        played += effect.played;
        skipped += effect.skipped;
    }
    SDL_LogDebug(SDL_LOG_CATEGORY_APPLICATION,
                 "audio: %s, device %s; Theme %s at %.2f s, loops %u, queued %.3f s; effects %u played, %u skipped; "
                 "music %s, sound effects %s; heap %zu KB in use",
                 unlocked_ ? "running" : "waiting for a gesture", paused_ ? "paused" : "playing",
                 music_.playing() ? "playing" : "stopped", static_cast<double>(music_.decodedSeconds()),
                 music_.loops(), static_cast<double>(music_.queuedSeconds()), played, skipped,
                 settings_.musicEnabled ? "on" : "off", settings_.soundEffectEnabled ? "on" : "off",
                 heapBytes / 1024);
}
#endif

} // namespace

void play(Cue cue) {
    gSystem.play(cue);
}

void setMusicEnabled(bool enabled) {
    gSystem.setMusic(enabled);
}

void setSoundEffectsEnabled(bool enabled) {
    gSystem.setEffects(enabled);
}

bool musicEnabled() {
    return gSystem.music();
}

bool soundEffectsEnabled() {
    return gSystem.effects();
}

void init(const char* assetRoot, const CueConfig& cues) {
    gSystem.init(assetRoot, cues);
}

void onEvent(const SDL_Event& event) {
    gSystem.onEvent(event);
}

void update() {
    gSystem.update();
}

void shutdown() {
    gSystem.shutdown();
}

} // namespace vibe::audio

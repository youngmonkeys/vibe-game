#include "vibe/audio/MusicStream.h"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>
#include <SDL3/SDL_stdinc.h>

#include <climits>

#include "vibe/audio/StbVorbis.h"

namespace vibe::audio {
namespace {

// Frames per decode call. 1024 frames is 23 ms at 44.1 kHz.
constexpr int kChunkFrames = 1024;

} // namespace

MusicStream::~MusicStream() {
    close();
}

bool MusicStream::open(const char* path, const SDL_AudioSpec& device, float gain) {
    close();
    file_ = SDL_LoadFile(path, &fileSize_);
    if (!file_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: cannot read %s: %s", path, SDL_GetError());
        return false;
    }
    if (fileSize_ > static_cast<std::size_t>(INT_MAX)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s is too large", path);
        close();
        return false;
    }
    int error = 0;
    vorbis_ = stb_vorbis_open_memory(static_cast<const unsigned char*>(file_), static_cast<int>(fileSize_), &error,
                                     nullptr);
    if (!vorbis_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s is not an Ogg Vorbis file stb_vorbis can read (error %d)",
                    path, error);
        close();
        return false;
    }
    const stb_vorbis_info info = stb_vorbis_get_info(vorbis_);
    channels_ = info.channels;
    sampleRate_ = static_cast<int>(info.sample_rate);
    if (channels_ < 1 || channels_ > 2 || sampleRate_ <= 0) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: %s has %d channels at %d Hz; expected 1 or 2", path,
                    channels_, sampleRate_);
        close();
        return false;
    }
    totalSamples_ = stb_vorbis_stream_length_in_samples(vorbis_);
    const SDL_AudioSpec source{SDL_AUDIO_F32, channels_, sampleRate_};
    stream_ = SDL_CreateAudioStream(&source, &device);
    if (!stream_) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_CreateAudioStream for %s failed: %s", path,
                    SDL_GetError());
        close();
        return false;
    }
    if (!SDL_SetAudioStreamGain(stream_, gain)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_SetAudioStreamGain failed: %s", SDL_GetError());
    }
    if (!SDL_SetAudioStreamGetCallback(stream_, onNeedData, this)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_SetAudioStreamGetCallback failed: %s", SDL_GetError());
        close();
        return false;
    }
    chunk_.assign(static_cast<std::size_t>(kChunkFrames * channels_), 0.0f);
    targetBytes_ = static_cast<int>(kQueueSeconds * static_cast<float>(sampleRate_)) * channels_ *
                   static_cast<int>(sizeof(float));
    SDL_Log("audio: music %s: %d Hz, %d channels, %.1f s, %zu bytes compressed; streamed %.2f s ahead", path,
            sampleRate_, channels_, static_cast<double>(totalSamples_) / sampleRate_, fileSize_,
            static_cast<double>(kQueueSeconds));
    return true;
}

void MusicStream::close() {
    playing_ = false;
    if (stream_) {
        SDL_DestroyAudioStream(stream_);
        stream_ = nullptr;
    }
    if (vorbis_) {
        stb_vorbis_close(vorbis_);
        vorbis_ = nullptr;
    }
    if (file_) {
        SDL_free(file_);
        file_ = nullptr;
    }
    fileSize_ = 0;
    chunk_.clear();
    chunk_.shrink_to_fit();
}

void MusicStream::start() {
    if (!vorbis_) {
        return;
    }
    SDL_ClearAudioStream(stream_);
    stb_vorbis_seek_start(vorbis_);
    playing_ = true;
    topUp();
}

void MusicStream::stop() {
    playing_ = false;
    if (stream_) {
        SDL_ClearAudioStream(stream_);
    }
}

#ifdef VIBE_DEV
void MusicStream::seekBeforeEnd(float seconds) {
    if (!vorbis_ || !playing_) {
        return;
    }
    const auto back = static_cast<unsigned>(seconds * static_cast<float>(sampleRate_));
    const unsigned target = totalSamples_ > back ? totalSamples_ - back : 0;
    SDL_ClearAudioStream(stream_);
    if (!stb_vorbis_seek(vorbis_, target)) {
        SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: seek to sample %u failed", target);
    }
    topUp();
}
#endif

float MusicStream::decodedSeconds() const {
    if (!vorbis_) {
        return 0.0f;
    }
    return static_cast<float>(stb_vorbis_get_playback_sample_offset(vorbis_)) / static_cast<float>(sampleRate_);
}

float MusicStream::queuedSeconds() const {
    if (!stream_) {
        return 0.0f;
    }
    const int queued = SDL_GetAudioStreamQueued(stream_);
    return queued <= 0 ? 0.0f
                       : static_cast<float>(queued) / static_cast<float>(sampleRate_ * channels_ * sizeof(float));
}

// SDL calls this when the device pulls from the stream. On Emscripten that
// is the ScriptProcessorNode callback, on the main thread (decision D15).
void SDLCALL MusicStream::onNeedData(void* userdata, SDL_AudioStream* /*stream*/, int /*additional*/,
                                     int /*total*/) {
    static_cast<MusicStream*>(userdata)->topUp();
}

void MusicStream::topUp() {
    if (!playing_ || !vorbis_) {
        return;
    }
    // Two seeks in a row without a sample means the file decodes to nothing;
    // stop instead of spinning.
    int emptyReads = 0;
    while (SDL_GetAudioStreamQueued(stream_) < targetBytes_) {
        const int frames = stb_vorbis_get_samples_float_interleaved(vorbis_, channels_, chunk_.data(),
                                                                    static_cast<int>(chunk_.size()));
        if (frames <= 0) {
            if (++emptyReads > 1) {
                if (!warnedDecode_) {
                    warnedDecode_ = true;
                    SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: music decodes to no samples; stopped");
                }
                playing_ = false;
                return;
            }
            // End of the file: loop from the first sample in the same top-up.
            stb_vorbis_seek_start(vorbis_);
            ++loops_;
            continue;
        }
        emptyReads = 0;
        if (!SDL_PutAudioStreamData(stream_, chunk_.data(), frames * channels_ * static_cast<int>(sizeof(float)))) {
            if (!warnedDecode_) {
                warnedDecode_ = true;
                SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, "audio: SDL_PutAudioStreamData failed: %s", SDL_GetError());
            }
            return;
        }
    }
}

} // namespace vibe::audio

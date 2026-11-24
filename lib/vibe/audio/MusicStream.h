#pragma once

#include <SDL3/SDL_audio.h>

#include <cstdint>
#include <vector>

struct stb_vorbis;

namespace vibe::audio {

// One looping Ogg Vorbis track, streamed (decision D4). The file stays
// compressed in memory and stb_vorbis decodes it a chunk at a time into an
// SDL_AudioStream, which converts to the device format. The stream's get
// callback keeps about kQueueSeconds of decoded audio queued, so the track
// never sits in memory decoded (129 s of stereo float would be 45 MB). At the
// end of the file the decoder seeks to 0 in the same top-up, so the loop has
// no gap.
class MusicStream {
public:
    static constexpr float kQueueSeconds = 0.25f;

    MusicStream() = default;
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;
    ~MusicStream();

    // Reads the file and opens the decoder. `device` is the output format.
    // Logs and returns false on failure.
    bool open(const char* path, const SDL_AudioSpec& device, float gain);
    void close();
    bool isOpen() const { return vorbis_ != nullptr; }

    // The stream to bind to the device. Null until open.
    SDL_AudioStream* stream() const { return stream_; }

    // From the start of the track.
    void start();
    void stop();
    bool playing() const { return playing_; }

#ifdef VIBE_DEV
    // Jumps to `seconds` before the end, to hear the loop (dev key).
    void seekBeforeEnd(float seconds);
#endif

    // Seconds into the track at the decoder (ahead of what is heard by the
    // queue), loops completed, and the queued amount in seconds.
    float decodedSeconds() const;
    unsigned loops() const { return loops_; }
    float queuedSeconds() const;
    std::size_t compressedBytes() const { return fileSize_; }

private:
    static void SDLCALL onNeedData(void* userdata, SDL_AudioStream* stream, int additional, int total);
    void topUp();

    void* file_ = nullptr; // SDL_LoadFile; stb_vorbis reads from it
    std::size_t fileSize_ = 0;
    stb_vorbis* vorbis_ = nullptr;
    SDL_AudioStream* stream_ = nullptr;
    int channels_ = 0;
    int sampleRate_ = 0;
    unsigned totalSamples_ = 0;
    int targetBytes_ = 0;
    std::vector<float> chunk_; // one decode, reused
    bool playing_ = false;
    unsigned loops_ = 0;
    bool warnedDecode_ = false;
};

} // namespace vibe::audio

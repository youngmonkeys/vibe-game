#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>

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

    MusicStream();
    MusicStream(const MusicStream&) = delete;
    MusicStream& operator=(const MusicStream&) = delete;
    ~MusicStream();

    // Reads the file, opens the decoder and binds the stream to `device`.
    // Logs and returns false on failure.
    bool open(const char* path, std::uint32_t device, float gain);
    void close();
    bool isOpen() const;

    // From the start of the track.
    void start();
    void stop();
    bool playing() const;

#ifdef VIBE_DEV
    // Jumps to `seconds` before the end, to hear the loop (dev key).
    void seekBeforeEnd(float seconds);
#endif

    // Seconds into the track at the decoder (ahead of what is heard by the
    // queue), loops completed, and the queued amount in seconds.
    float decodedSeconds() const;
    unsigned loops() const;
    float queuedSeconds() const;
    std::size_t compressedBytes() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vibe::audio

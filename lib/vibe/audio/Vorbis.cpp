// The one translation unit that compiles stb_vorbis.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#include <stb_vorbis.h>

#include "vibe/audio/Vorbis.h"

namespace vibe::audio {

int decodeVorbis(const void* data, int size, int& channels, int& rate, short*& pcm) {
    return stb_vorbis_decode_memory(static_cast<const unsigned char*>(data), size, &channels, &rate, &pcm);
}

} // namespace vibe::audio

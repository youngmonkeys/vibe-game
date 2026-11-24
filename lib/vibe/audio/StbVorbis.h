#pragma once

// stb_vorbis v1.22 as shipped in SDL_mixer release-3.2.4
// (research/dependencies.md). Decoding from memory only: no stdio, no push
// API. The include directory is SYSTEM, so the decoder's own warnings never
// trip -Werror. StbVorbis.cpp compiles the implementation.
#define STB_VORBIS_NO_STDIO
#define STB_VORBIS_NO_PUSHDATA_API
#define STB_VORBIS_HEADER_ONLY
#include <stb_vorbis.h>
#undef STB_VORBIS_HEADER_ONLY

#pragma once

namespace vibe::audio {

int decodeVorbis(const void* data, int size, int& channels, int& rate, short*& pcm);

} // namespace vibe::audio

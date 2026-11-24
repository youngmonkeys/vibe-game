#pragma once

#include <cstdint>

namespace vibe::net {

// Text bytes and frames through the socket since the transport was made,
// counted in the callbacks (plan 08's dev overlay). `bytes` is the frame text
// only; the WebSocket header adds 2 to 14 bytes per frame on the wire.
struct TrafficCounters {
    std::uint64_t bytesOut = 0;
    std::uint64_t framesOut = 0;
    std::uint64_t bytesIn = 0;
    std::uint64_t framesIn = 0;
    // Stream messages (isStreamMessage) decoded, before the inbound queue coalesces them.
    std::uint64_t streamIn = 0;
    std::uint64_t udpFramesIn = 0;
    std::uint64_t udpStreamIn = 0;
};

} // namespace vibe::net

#include "vibe/net/DecodeSupport.h"
#include "vibe/net/Queues.h"

namespace vibe::net {

DecodeResult decodeGameCommand(const std::string& /*command*/, const Json& /*payload*/, InboundMessage& /*out*/,
                               bool& handled) {
    handled = false;
    return DecodeResult::ok();
}

InboundMessage decodeGameHttp(HttpStep step, int status, std::string_view /*body*/, double elapsedMs,
                              bool /*firstPage*/) {
    return failure(step, status, elapsedMs, "the game has no HTTP API yet");
}

const char* gameHttpStepName(HttpStep /*step*/) {
    return "game";
}

bool isStreamMessage(const InboundMessage& /*message*/) {
    return false;
}

bool coalesceStream(InboundMessage& /*queued*/, const InboundMessage& /*incoming*/) {
    return false;
}

} // namespace vibe::net

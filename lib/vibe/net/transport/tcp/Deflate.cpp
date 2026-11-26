#include "vibe/net/transport/tcp/Deflate.h"

#include <zlib.h>

#include <algorithm>
#include <limits>

namespace vibe::net {
namespace {

constexpr int kRawWindowBits = -15;
constexpr int kMemoryLevel = 8;
constexpr std::size_t kMinInflateBytes = 1024;

void wipe(std::string& data) {
    std::fill(data.begin(), data.end(), '\0');
    data.clear();
}

} // namespace

bool deflateRaw(std::string_view input, std::string& out) {
    wipe(out);
    if (input.size() > std::numeric_limits<uInt>::max()) {
        return false;
    }
    z_stream stream{};
    if (deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, kRawWindowBits, kMemoryLevel, Z_DEFAULT_STRATEGY) !=
        Z_OK) {
        return false;
    }
    out.resize(deflateBound(&stream, static_cast<uLong>(input.size())));
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = reinterpret_cast<Bytef*>(out.data());
    stream.avail_out = static_cast<uInt>(out.size());
    const int result = deflate(&stream, Z_FINISH);
    const std::size_t written = out.size() - stream.avail_out;
    deflateEnd(&stream);
    if (result != Z_STREAM_END) {
        wipe(out);
        return false;
    }
    out.resize(written);
    return true;
}

bool inflateRaw(std::string_view input, std::size_t maxBytes, std::string& out, std::string& error) {
    wipe(out);
    if (input.empty() || input.size() > std::numeric_limits<uInt>::max()) {
        error = "compressed data has a bad size";
        return false;
    }
    z_stream stream{};
    if (inflateInit2(&stream, kRawWindowBits) != Z_OK) {
        error = "cannot start the inflater";
        return false;
    }
    stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
    stream.avail_in = static_cast<uInt>(input.size());
    out.resize(std::min(maxBytes + 1, std::max(kMinInflateBytes, input.size() * 4)));
    std::size_t length = 0;
    int result = Z_OK;
    while (result != Z_STREAM_END) {
        if (length == out.size()) {
            if (out.size() > maxBytes) {
                break;
            }
            out.resize(std::min(maxBytes + 1, out.size() * 2));
        }
        stream.next_out = reinterpret_cast<Bytef*>(out.data() + length);
        stream.avail_out = static_cast<uInt>(out.size() - length);
        result = inflate(&stream, Z_NO_FLUSH);
        length = out.size() - stream.avail_out;
        if (result != Z_OK && result != Z_STREAM_END) {
            break;
        }
        if (result == Z_OK && stream.avail_in == 0 && stream.avail_out > 0) {
            result = Z_BUF_ERROR;
            break;
        }
    }
    inflateEnd(&stream);
    if (length > maxBytes) {
        wipe(out);
        error = "inflated data is larger than the inbound cap";
        return false;
    }
    if (result != Z_STREAM_END) {
        wipe(out);
        error = result == Z_BUF_ERROR ? "compressed data is truncated" : "compressed data is not valid";
        return false;
    }
    if (stream.avail_in != 0) {
        wipe(out);
        error = "bytes left after the compressed data";
        return false;
    }
    out.resize(length);
    return true;
}

} // namespace vibe::net

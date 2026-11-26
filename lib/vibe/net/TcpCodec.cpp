#include "vibe/net/TcpCodec.h"

#include <algorithm>
#include <cstring>
#include <limits>
#include <nlohmann/json.hpp>

namespace vibe::net {
namespace {

using Json = nlohmann::json;

struct Cursor {
    const unsigned char* data = nullptr;
    std::size_t size = 0;
    std::size_t pos = 0;
    bool keepBinary = false;

    std::size_t left() const { return size - pos; }
};

bool fail(std::string& error, const char* reason) {
    error = reason;
    return false;
}

std::uint64_t readBigEndian(Cursor& c, std::size_t bytes) {
    std::uint64_t value = 0;
    for (std::size_t i = 0; i < bytes; ++i) {
        value = (value << 8) | c.data[c.pos + i];
    }
    c.pos += bytes;
    return value;
}

std::string base64(const unsigned char* data, std::size_t size) {
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve((size + 2) / 3 * 4);
    std::size_t i = 0;
    for (; i + 3 <= size; i += 3) {
        const std::uint32_t n = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8) | data[i + 2];
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back(kAlphabet[n & 63]);
    }
    if (i + 1 == size) {
        const std::uint32_t n = std::uint32_t{data[i]} << 16;
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out += "==";
    } else if (i + 2 == size) {
        const std::uint32_t n = (std::uint32_t{data[i]} << 16) | (std::uint32_t{data[i + 1]} << 8);
        out.push_back(kAlphabet[(n >> 18) & 63]);
        out.push_back(kAlphabet[(n >> 12) & 63]);
        out.push_back(kAlphabet[(n >> 6) & 63]);
        out.push_back('=');
    }
    return out;
}

Json unsignedValue(std::uint64_t value) {
    if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) {
        return Json(static_cast<std::int64_t>(value));
    }
    return Json(value);
}

bool readValue(Cursor& c, int depth, Json& out, std::string& error);

bool readString(Cursor& c, std::size_t length, Json& out, std::string& error) {
    if (c.left() < length) {
        return fail(error, "string runs past the end");
    }
    out = std::string(reinterpret_cast<const char*>(c.data + c.pos), length);
    c.pos += length;
    return true;
}

bool readBinary(Cursor& c, std::size_t length, Json& out, std::string& error) {
    if (c.left() < length) {
        return fail(error, "binary runs past the end");
    }
    if (c.keepBinary) {
        out = Json::binary(Json::binary_t::container_type(c.data + c.pos, c.data + c.pos + length));
    } else {
        out = base64(c.data + c.pos, length);
    }
    c.pos += length;
    return true;
}

bool readArray(Cursor& c, std::size_t count, int depth, Json& out, std::string& error) {
    if (count > c.left()) {
        return fail(error, "array longer than the frame");
    }
    out = Json::array();
    for (std::size_t i = 0; i < count; ++i) {
        Json item;
        if (!readValue(c, depth + 1, item, error)) {
            return false;
        }
        out.push_back(std::move(item));
    }
    return true;
}

bool readMap(Cursor& c, std::size_t count, int depth, Json& out, std::string& error) {
    if (count > c.left() / 2) {
        return fail(error, "map longer than the frame");
    }
    out = Json::object();
    for (std::size_t i = 0; i < count; ++i) {
        Json key;
        Json value;
        if (!readValue(c, depth + 1, key, error) || !readValue(c, depth + 1, value, error)) {
            return false;
        }
        if (!key.is_string()) {
            return fail(error, "map key is not a string");
        }
        out[key.get_ref<const std::string&>()] = std::move(value);
    }
    return true;
}

bool need(Cursor& c, std::size_t bytes, std::string& error) {
    if (c.left() < bytes) {
        return fail(error, "value runs past the end");
    }
    return true;
}

bool readValue(Cursor& c, int depth, Json& out, std::string& error) {
    if (depth > kMsgPackMaxDepth) {
        return fail(error, "nested too deep");
    }
    if (!need(c, 1, error)) {
        return false;
    }
    const unsigned char type = c.data[c.pos++];
    if (type <= 0x7f) {
        out = Json(static_cast<std::int64_t>(type));
        return true;
    }
    if (type >= 0xe0) {
        out = Json(static_cast<std::int64_t>(static_cast<std::int8_t>(type)));
        return true;
    }
    if (type >= 0x80 && type <= 0x8f) {
        return readMap(c, type & 0x0f, depth, out, error);
    }
    if (type >= 0x90 && type <= 0x9f) {
        return readArray(c, type & 0x0f, depth, out, error);
    }
    if (type >= 0xa0 && type <= 0xbf) {
        return readString(c, type & 0x1f, out, error);
    }
    switch (type) {
    case 0xc0: out = nullptr; return true;
    case 0xc2: out = false; return true;
    case 0xc3: out = true; return true;
    case 0xc4:
        if (!need(c, 1, error)) return false;
        return readBinary(c, readBigEndian(c, 1), out, error);
    case 0xc5:
        if (!need(c, 2, error)) return false;
        return readBinary(c, readBigEndian(c, 2), out, error);
    case 0xc6:
        if (!need(c, 4, error)) return false;
        return readBinary(c, readBigEndian(c, 4), out, error);
    case 0xca: {
        if (!need(c, 4, error)) return false;
        const auto bits = static_cast<std::uint32_t>(readBigEndian(c, 4));
        float value = 0.0f;
        std::memcpy(&value, &bits, sizeof value);
        out = static_cast<double>(value);
        return true;
    }
    case 0xcb: {
        if (!need(c, 8, error)) return false;
        const std::uint64_t bits = readBigEndian(c, 8);
        double value = 0.0;
        std::memcpy(&value, &bits, sizeof value);
        out = value;
        return true;
    }
    case 0xcc:
        if (!need(c, 1, error)) return false;
        out = unsignedValue(readBigEndian(c, 1));
        return true;
    case 0xcd:
        if (!need(c, 2, error)) return false;
        out = unsignedValue(readBigEndian(c, 2));
        return true;
    case 0xce:
        if (!need(c, 4, error)) return false;
        out = unsignedValue(readBigEndian(c, 4));
        return true;
    case 0xcf:
        if (!need(c, 8, error)) return false;
        out = unsignedValue(readBigEndian(c, 8));
        return true;
    case 0xd0:
        if (!need(c, 1, error)) return false;
        out = Json(static_cast<std::int64_t>(static_cast<std::int8_t>(readBigEndian(c, 1))));
        return true;
    case 0xd1:
        if (!need(c, 2, error)) return false;
        out = Json(static_cast<std::int64_t>(static_cast<std::int16_t>(readBigEndian(c, 2))));
        return true;
    case 0xd2:
        if (!need(c, 4, error)) return false;
        out = Json(static_cast<std::int64_t>(static_cast<std::int32_t>(readBigEndian(c, 4))));
        return true;
    case 0xd3:
        if (!need(c, 8, error)) return false;
        out = Json(static_cast<std::int64_t>(readBigEndian(c, 8)));
        return true;
    case 0xd9:
        if (!need(c, 1, error)) return false;
        return readString(c, readBigEndian(c, 1), out, error);
    case 0xda:
        if (!need(c, 2, error)) return false;
        return readString(c, readBigEndian(c, 2), out, error);
    case 0xdb:
        if (!need(c, 4, error)) return false;
        return readString(c, readBigEndian(c, 4), out, error);
    case 0xdc:
        if (!need(c, 2, error)) return false;
        return readArray(c, readBigEndian(c, 2), depth, out, error);
    case 0xdd:
        if (!need(c, 4, error)) return false;
        return readArray(c, readBigEndian(c, 4), depth, out, error);
    case 0xde:
        if (!need(c, 2, error)) return false;
        return readMap(c, readBigEndian(c, 2), depth, out, error);
    case 0xdf:
        if (!need(c, 4, error)) return false;
        return readMap(c, readBigEndian(c, 4), depth, out, error);
    default: return fail(error, "unsupported MessagePack type");
    }
}

void writeBigEndian(std::string& out, std::uint64_t value, std::size_t bytes) {
    for (std::size_t i = bytes; i > 0; --i) {
        out.push_back(static_cast<char>((value >> ((i - 1) * 8)) & 0xff));
    }
}

void writeType(std::string& out, unsigned char type) {
    out.push_back(static_cast<char>(type));
}

void writeUnsigned(std::string& out, std::uint64_t value) {
    if (value <= 0x7f) {
        writeType(out, static_cast<unsigned char>(value));
    } else if (value <= 0xff) {
        writeType(out, 0xcc);
        writeBigEndian(out, value, 1);
    } else if (value <= 0xffff) {
        writeType(out, 0xcd);
        writeBigEndian(out, value, 2);
    } else if (value <= static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())) {
        writeType(out, 0xce);
        writeBigEndian(out, value, 4);
    } else {
        writeType(out, 0xcf);
        writeBigEndian(out, value, 8);
    }
}

void writeSigned(std::string& out, std::int64_t value) {
    if (value >= 0) {
        writeUnsigned(out, static_cast<std::uint64_t>(value));
    } else if (value >= -32) {
        writeType(out, static_cast<unsigned char>(static_cast<std::int8_t>(value)));
    } else if (value >= std::numeric_limits<std::int8_t>::min()) {
        writeType(out, 0xd0);
        writeBigEndian(out, static_cast<std::uint8_t>(value), 1);
    } else if (value >= std::numeric_limits<std::int16_t>::min()) {
        writeType(out, 0xd1);
        writeBigEndian(out, static_cast<std::uint16_t>(value), 2);
    } else if (value >= std::numeric_limits<std::int32_t>::min()) {
        writeType(out, 0xd2);
        writeBigEndian(out, static_cast<std::uint32_t>(value), 4);
    } else {
        writeType(out, 0xd3);
        writeBigEndian(out, static_cast<std::uint64_t>(value), 8);
    }
}

void writeLength(std::string& out, std::size_t length, unsigned char fixBase, std::size_t fixMax, unsigned char type8,
                 unsigned char type16, unsigned char type32) {
    if (length <= fixMax) {
        writeType(out, static_cast<unsigned char>(fixBase | length));
    } else if (type8 != 0 && length <= 0xff) {
        writeType(out, type8);
        writeBigEndian(out, length, 1);
    } else if (length <= 0xffff) {
        writeType(out, type16);
        writeBigEndian(out, length, 2);
    } else {
        writeType(out, type32);
        writeBigEndian(out, length, 4);
    }
}

void writeValue(const Json& value, std::string& out) {
    switch (value.type()) {
    case Json::value_t::null:
    case Json::value_t::discarded: writeType(out, 0xc0); return;
    case Json::value_t::boolean: writeType(out, value.get<bool>() ? 0xc3 : 0xc2); return;
    case Json::value_t::number_integer: writeSigned(out, value.get<std::int64_t>()); return;
    case Json::value_t::number_unsigned: writeUnsigned(out, value.get<std::uint64_t>()); return;
    case Json::value_t::number_float: {
        const double number = value.get<double>();
        std::uint64_t bits = 0;
        std::memcpy(&bits, &number, sizeof bits);
        writeType(out, 0xcb);
        writeBigEndian(out, bits, 8);
        return;
    }
    case Json::value_t::string: {
        const auto& text = value.get_ref<const std::string&>();
        writeLength(out, text.size(), 0xa0, 31, 0xd9, 0xda, 0xdb);
        out += text;
        return;
    }
    case Json::value_t::binary: {
        const auto& bytes = value.get_binary();
        if (bytes.size() <= 0xff) {
            writeType(out, 0xc4);
            writeBigEndian(out, bytes.size(), 1);
        } else if (bytes.size() <= 0xffff) {
            writeType(out, 0xc5);
            writeBigEndian(out, bytes.size(), 2);
        } else {
            writeType(out, 0xc6);
            writeBigEndian(out, bytes.size(), 4);
        }
        out.append(reinterpret_cast<const char*>(bytes.data()), bytes.size());
        return;
    }
    case Json::value_t::array:
        writeLength(out, value.size(), 0x90, 15, 0, 0xdc, 0xdd);
        for (const auto& item : value) {
            writeValue(item, out);
        }
        return;
    case Json::value_t::object:
        writeLength(out, value.size(), 0x80, 15, 0, 0xde, 0xdf);
        for (auto it = value.begin(); it != value.end(); ++it) {
            writeLength(out, it.key().size(), 0xa0, 31, 0xd9, 0xda, 0xdb);
            out += it.key();
            writeValue(it.value(), out);
        }
        return;
    }
}

bool isHandshake(const Json& frame) {
    return frame.is_array() && frame.size() == 2 && frame[0].is_number_integer() &&
           frame[0].get<std::int64_t>() == 11 && frame[1].is_array() && frame[1].size() > 1;
}

bool isSecretAppRequest(const Json& frame) {
    if (frame.size() != 2 || !frame[1].is_array() || frame[1].size() != 2 || !frame[1][1].is_array() ||
        frame[1][1].empty() || !frame[1][1][0].is_string()) {
        return false;
    }
    const std::string& name = frame[1][1][0].get_ref<const std::string&>();
    return name == "lobbyChat" || name == "roomChat";
}

bool isSecret(const Json& frame, int command) {
    return command == kTcpCommandLogin || (command == kTcpCommandAppRequest && isSecretAppRequest(frame));
}

bool msgpackToJson(std::string_view bytes, Json& out, std::string& error) {
    Cursor c{reinterpret_cast<const unsigned char*>(bytes.data()), bytes.size(), 0};
    if (!readValue(c, 0, out, error)) {
        return false;
    }
    if (c.pos != c.size) {
        return fail(error, "bytes left after the value");
    }
    return true;
}

void jsonToMsgpack(const Json& value, std::string& out) {
    writeValue(value, out);
}

} // namespace

bool msgpackToJsonText(std::string_view bytes, std::string& jsonText, std::string& error) {
    Json value;
    if (!msgpackToJson(bytes, value, error)) {
        return false;
    }
    jsonText = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    return true;
}

bool jsonTextToMsgpack(std::string_view jsonText, std::string& out) {
    const Json value = Json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (value.is_discarded()) {
        return false;
    }
    out.clear();
    jsonToMsgpack(value, out);
    return true;
}

bool encodeTcpContent(std::string_view jsonFrame, std::string_view clientKey, bool requestCompression,
                      std::string& content, int& command, bool& secret, std::string& error) {
    Json frame = Json::parse(jsonFrame.begin(), jsonFrame.end(), nullptr, false);
    if (frame.is_discarded()) {
        return fail(error, "frame is not JSON");
    }
    command = frame.is_array() && !frame.empty() && frame[0].is_number_integer() ? frame[0].get<int>() : 0;
    secret = isSecret(frame, command);
    if (isHandshake(frame)) {
        Json& data = frame[1];
        if (clientKey.empty()) {
            if (data[1].is_string() && data[1].get_ref<const std::string&>().empty()) {
                data[1] = nullptr;
            }
        } else {
            data[1] = Json::binary(Json::binary_t::container_type(clientKey.begin(), clientKey.end()));
            if (data.size() > 4) {
                data[4] = true;
            }
        }
        if (requestCompression) {
            while (data.size() < kTcpHandshakeCompressionIndex) {
                data.push_back(nullptr);
            }
            if (data.size() == kTcpHandshakeCompressionIndex) {
                data.push_back(true);
            } else {
                data[kTcpHandshakeCompressionIndex] = true;
            }
        }
    }
    content.clear();
    jsonToMsgpack(frame, content);
    return true;
}

void writeTcpFrame(std::uint8_t flags, std::string_view content, std::string& out) {
    const bool big = content.size() > kMaxSmallTcpContentBytes;
    out.clear();
    out.reserve(content.size() + 5);
    out.push_back(static_cast<char>(big ? (flags | kTcpHeaderBigSize) : (flags & ~kTcpHeaderBigSize)));
    writeBigEndian(out, content.size(), big ? 4 : 2);
    out.append(content);
}

bool encodeTcpFrame(std::string_view jsonFrame, std::string& out, std::string& error) {
    std::string content;
    int command = 0;
    bool secret = false;
    if (!encodeTcpContent(jsonFrame, {}, false, content, command, secret, error)) {
        return false;
    }
    writeTcpFrame(0, content, out);
    std::fill(content.begin(), content.end(), '\0');
    return true;
}

bool decodeTcpContent(std::uint8_t header, std::string_view content, std::string& jsonFrame, std::string& reason) {
    if (header & kTcpHeaderEncrypted) {
        reason = "an encrypted frame without a session key";
        return false;
    }
    if (header & kTcpHeaderCompressed) {
        reason = "a compressed frame that was not inflated";
        return false;
    }
    if (header & kTcpHeaderRawBytes) {
        reason = "a raw bytes frame (streaming is off)";
        return false;
    }
    if (header & kTcpHeaderText) {
        jsonFrame.assign(content);
        return true;
    }
    Json value;
    std::string error;
    if (!msgpackToJson(content, value, error)) {
        reason = "a malformed MessagePack frame: " + error;
        return false;
    }
    jsonFrame = value.dump(-1, ' ', false, Json::error_handler_t::replace);
    return true;
}

namespace {

bool readCommandFrame(std::string_view content, int command, std::size_t minDataSize, Json& frame) {
    Cursor c{reinterpret_cast<const unsigned char*>(content.data()), content.size(), 0, true};
    std::string error;
    if (!readValue(c, 0, frame, error) || c.pos != c.size) {
        return false;
    }
    return frame.is_array() && frame.size() == 2 && frame[0].is_number_integer() &&
           frame[0].get<std::int64_t>() == command && frame[1].is_array() && frame[1].size() >= minDataSize;
}

} // namespace

bool readTcpHandshake(std::string_view content, std::string& key, std::string& proof) {
    key.clear();
    proof.clear();
    Json frame;
    if (!readCommandFrame(content, kTcpCommandHandshake, 3, frame)) {
        return false;
    }
    if (frame[1][0].is_binary()) {
        const auto& bytes = frame[1][0].get_binary();
        proof.assign(bytes.begin(), bytes.end());
    }
    if (frame[1].size() > 3 && frame[1][3].is_binary()) {
        const auto& bytes = frame[1][3].get_binary();
        key.assign(bytes.begin(), bytes.end());
    }
    return true;
}

bool readTcpHandshakeCompression(std::string_view content) {
    Json frame;
    if (!readCommandFrame(content, kTcpCommandHandshake, kTcpHandshakeAckCompressionIndex + 1, frame)) {
        return false;
    }
    const Json& flag = frame[1][kTcpHandshakeAckCompressionIndex];
    return flag.is_boolean() && flag.get<bool>();
}

bool readTcpHandshakeSession(std::string_view content, std::int64_t& sessionId, std::string& token) {
    sessionId = 0;
    token.clear();
    Json frame;
    if (!readCommandFrame(content, kTcpCommandHandshake, 3, frame)) {
        return false;
    }
    const Json& data = frame[1];
    if (!data[1].is_string() || !data[2].is_number_integer()) {
        return false;
    }
    token = data[1].get<std::string>();
    sessionId = data[2].get<std::int64_t>();
    return !token.empty() && token.size() <= kMaxUdpHandshakeTokenBytes;
}

bool writeUdpHandshake(std::int64_t sessionId, std::string_view token, std::string& out) {
    out.clear();
    if (token.empty() || token.size() > kMaxUdpHandshakeTokenBytes) {
        return false;
    }
    std::string content;
    content.reserve(10 + token.size());
    writeBigEndian(content, static_cast<std::uint64_t>(sessionId), 8);
    writeBigEndian(content, token.size(), 2);
    content.append(token);
    writeTcpFrame(kTcpHeaderUdpHandshake, content, out);
    std::fill(content.begin(), content.end(), '\0');
    return true;
}

bool readUdpHandshake(std::string_view content, int& code) {
    code = 0;
    Json frame;
    if (!readCommandFrame(content, kTcpCommandUdpHandshake, 1, frame) || !frame[1][0].is_number_integer()) {
        return false;
    }
    code = static_cast<int>(frame[1][0].get<std::int64_t>());
    return true;
}

namespace {

bool readProofField(std::string_view bytes, std::size_t& pos, std::string& out) {
    if (bytes.size() - pos < 2) {
        return false;
    }
    const std::size_t length = (static_cast<unsigned char>(bytes[pos]) << 8) | static_cast<unsigned char>(bytes[pos + 1]);
    pos += 2;
    if (length == 0 || bytes.size() - pos < length) {
        return false;
    }
    out.assign(bytes.substr(pos, length));
    pos += length;
    return true;
}

void appendProofField(std::string& out, std::string_view value) {
    out.push_back(static_cast<char>((value.size() >> 8) & 0xff));
    out.push_back(static_cast<char>(value.size() & 0xff));
    out.append(value);
}

} // namespace

bool parseTcpServerProof(std::string_view bytes, TcpServerProof& out) {
    out.signature.clear();
    out.certificates.clear();
    if (bytes.size() < 2 || static_cast<std::uint8_t>(bytes[0]) != kTcpServerProofVersion) {
        return false;
    }
    std::size_t pos = 1;
    if (!readProofField(bytes, pos, out.signature) || pos >= bytes.size()) {
        return false;
    }
    const std::size_t count = static_cast<unsigned char>(bytes[pos++]);
    if (count == 0 || count > 8) {
        return false;
    }
    out.certificates.resize(count);
    for (std::string& certificate : out.certificates) {
        if (!readProofField(bytes, pos, certificate)) {
            return false;
        }
    }
    return pos == bytes.size();
}

std::string tcpHandshakeSignedMessage(std::string_view clientKey, std::string_view encryptedKey) {
    std::string out(kTcpHandshakeSignatureDomain);
    appendProofField(out, clientKey);
    appendProofField(out, encryptedKey);
    return out;
}

void TcpFrameReader::append(const char* data, std::size_t size) {
    buffer_.append(data, size);
}

void TcpFrameReader::reset() {
    std::fill(buffer_.begin(), buffer_.end(), '\0');
    buffer_.clear();
    offset_ = 0;
    skipRemaining_ = 0;
}

void TcpFrameReader::compact() {
    if (offset_ == buffer_.size()) {
        std::fill(buffer_.begin(), buffer_.end(), '\0');
        buffer_.clear();
        offset_ = 0;
    } else if (offset_ > 64 * 1024) {
        std::fill(buffer_.begin(), buffer_.begin() + static_cast<std::ptrdiff_t>(offset_), '\0');
        buffer_.erase(0, offset_);
        offset_ = 0;
    }
}

TcpFrameReader::Next TcpFrameReader::next(std::string& jsonFrame, std::string& reason) {
    std::uint8_t header = 0;
    std::string content;
    const Next result = nextRaw(header, content, reason);
    if (result != Next::Frame) {
        return result;
    }
    const bool ok = decodeTcpContent(header, content, jsonFrame, reason);
    std::fill(content.begin(), content.end(), '\0');
    return ok ? Next::Frame : Next::Skipped;
}

TcpFrameReader::Next TcpFrameReader::nextRaw(std::uint8_t& header, std::string& content, std::string& reason) {
    if (skipRemaining_ > 0) {
        const std::size_t n = std::min(skipRemaining_, buffered());
        offset_ += n;
        skipRemaining_ -= n;
        if (skipRemaining_ > 0) {
            compact();
            return Next::NeedMore;
        }
    }
    if (buffered() < 1) {
        compact();
        return Next::NeedMore;
    }
    header = static_cast<std::uint8_t>(buffer_[offset_]);
    const std::size_t sizeBytes = (header & kTcpHeaderBigSize) ? 4 : 2;
    if (buffered() < 1 + sizeBytes) {
        compact();
        return Next::NeedMore;
    }
    std::size_t size = 0;
    for (std::size_t i = 0; i < sizeBytes; ++i) {
        size = (size << 8) | static_cast<unsigned char>(buffer_[offset_ + 1 + i]);
    }
    if (size == 0 || size > maxContentBytes_) {
        offset_ += 1 + sizeBytes;
        skipRemaining_ = size;
        reason = size == 0 ? "an empty frame" : "a frame larger than the inbound cap";
        return Next::Skipped;
    }
    if (buffered() < 1 + sizeBytes + size) {
        compact();
        return Next::NeedMore;
    }
    content.assign(buffer_.data() + offset_ + 1 + sizeBytes, size);
    offset_ += 1 + sizeBytes + size;
    return Next::Frame;
}

} // namespace vibe::net

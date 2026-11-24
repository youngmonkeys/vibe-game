#pragma once

#include <cstddef>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include "vibe/net/Encode.h"
#include "vibe/net/Messages.h"
#include "vibe/net/Protocol.h"

namespace vibe::net {

// Bounded queues between the network callbacks and the frame
// (docs/architecture.md, Network callbacks). One thread, so no lock; the
// push and pop API stays so threads could be added without touching callers.
// Storage is reserved up front. A full queue refuses the push and remembers
// it, and the session disconnects instead of growing.

class InboundQueue {
public:
    // A RoomPlayers message is about 5 KB, so 128 slots is about 640 KB.
    static constexpr std::size_t kCapacity = 128;

    explicit InboundQueue(const Protocol& protocol) : protocol_(&protocol), slots_(kCapacity) {}

    // `s` is coalesced: a newer pose for a player still in the queue replaces
    // the older one in place, so only the newest is drained. Returns false,
    // and sets overflowed(), when the queue is full.
    bool push(InboundMessage&& message) {
        if (protocol_->isStream(message)) {
            for (std::size_t i = 0; i < size_; ++i) {
                if (protocol_->coalesce(slots_[(head_ + i) % kCapacity], message)) {
                    ++coalesced_;
                    return true;
                }
            }
        }
        if (size_ == kCapacity) {
            overflowed_ = true;
            return false;
        }
        slots_[(head_ + size_) % kCapacity] = std::move(message);
        ++size_;
        return true;
    }

    bool pop(InboundMessage& out) {
        if (size_ == 0) {
            return false;
        }
        out = std::move(slots_[head_]);
        slots_[head_] = std::monostate{};
        head_ = (head_ + 1) % kCapacity;
        --size_;
        return true;
    }

    void clear() {
        while (size_ > 0) {
            slots_[head_] = std::monostate{};
            head_ = (head_ + 1) % kCapacity;
            --size_;
        }
        overflowed_ = false;
    }

    std::size_t size() const { return size_; }
    bool overflowed() const { return overflowed_; }
    std::size_t coalescedCount() const { return coalesced_; }
    const Protocol& protocol() const { return *protocol_; }

private:
    const Protocol* protocol_;
    std::vector<InboundMessage> slots_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    std::size_t coalesced_ = 0;
    bool overflowed_ = false;
};

class OutboundQueue {
public:
    static constexpr std::size_t kCapacity = 64;

    OutboundQueue() : slots_(kCapacity) {
        for (auto& slot : slots_) {
            slot.reserve(kMaxOutboundFrameBytes);
        }
    }

    // Copies `frame` into reserved storage. Refuses an empty frame, one over
    // the server's 2048-byte limit, and a push to a full queue (which also
    // sets overflowed()).
    bool push(std::string_view frame) {
        if (frame.empty() || frame.size() > kMaxOutboundFrameBytes) {
            return false;
        }
        if (size_ == kCapacity) {
            overflowed_ = true;
            return false;
        }
        slots_[(head_ + size_) % kCapacity].assign(frame);
        ++size_;
        return true;
    }

    // The oldest frame, valid until the next pop or push.
    const std::string* front() const { return size_ == 0 ? nullptr : &slots_[head_]; }

    void pop() {
        if (size_ == 0) {
            return;
        }
        // Overwrite, so a token in the login frame does not linger in memory
        // longer than it has to.
        slots_[head_].assign(slots_[head_].size(), '\0');
        slots_[head_].clear();
        head_ = (head_ + 1) % kCapacity;
        --size_;
    }

    void clear() {
        while (size_ > 0) {
            pop();
        }
        overflowed_ = false;
    }

    std::size_t size() const { return size_; }
    bool overflowed() const { return overflowed_; }

private:
    std::vector<std::string> slots_;
    std::size_t head_ = 0;
    std::size_t size_ = 0;
    bool overflowed_ = false;
};

} // namespace vibe::net

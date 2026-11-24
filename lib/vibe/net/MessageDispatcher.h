#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <utility>
#include <variant>
#include <vector>

#include "vibe/net/Messages.h"

namespace vibe::net {

namespace detail {

struct DispatchTable {
    struct Handler {
        std::uint64_t id = 0;
        std::function<void(const InboundMessage&)> call;
    };

    std::vector<std::deque<Handler>> slots = std::vector<std::deque<Handler>>(kInboundMessageCount + 1);
    std::uint64_t nextId = 1;
    int depth = 0;
    bool dirty = false;

    bool run(std::size_t slot, const InboundMessage& message) {
        auto& handlers = slots[slot];
        bool ran = false;
        for (std::size_t i = 0, count = handlers.size(); i < count; ++i) {
            if (handlers[i].id != 0) {
                ran = true;
                handlers[i].call(message);
            }
        }
        return ran;
    }

    void remove(std::size_t slot, std::uint64_t id) {
        auto& handlers = slots[slot];
        for (auto it = handlers.begin(); it != handlers.end(); ++it) {
            if (it->id != id) {
                continue;
            }
            if (depth > 0) {
                it->id = 0;
                dirty = true;
            } else {
                handlers.erase(it);
            }
            return;
        }
    }

    void compact() {
        for (auto& handlers : slots) {
            handlers.erase(std::remove_if(handlers.begin(), handlers.end(),
                                          [](const Handler& handler) { return handler.id == 0; }),
                           handlers.end());
        }
        dirty = false;
    }
};

} // namespace detail

class Subscription {
public:
    Subscription() = default;
    Subscription(const Subscription&) = delete;
    Subscription& operator=(const Subscription&) = delete;

    Subscription(Subscription&& other) noexcept
        : table_(std::move(other.table_)), slot_(other.slot_), id_(std::exchange(other.id_, 0)) {}

    Subscription& operator=(Subscription&& other) noexcept {
        if (this != &other) {
            reset();
            table_ = std::move(other.table_);
            slot_ = other.slot_;
            id_ = std::exchange(other.id_, 0);
        }
        return *this;
    }

    ~Subscription() { reset(); }

    void reset() {
        if (id_ != 0) {
            if (const auto table = table_.lock()) {
                table->remove(slot_, id_);
            }
        }
        table_.reset();
        id_ = 0;
    }

    explicit operator bool() const { return id_ != 0 && !table_.expired(); }

private:
    friend class MessageDispatcher;

    Subscription(std::weak_ptr<detail::DispatchTable> table, std::size_t slot, std::uint64_t id)
        : table_(std::move(table)), slot_(slot), id_(id) {}

    std::weak_ptr<detail::DispatchTable> table_;
    std::size_t slot_ = 0;
    std::uint64_t id_ = 0;
};

class Subscriptions {
public:
    Subscriptions& operator+=(Subscription subscription) {
        list_.push_back(std::move(subscription));
        return *this;
    }

    void clear() { list_.clear(); }
    bool empty() const { return list_.empty(); }

private:
    std::vector<Subscription> list_;
};

class MessageDispatcher {
public:
    MessageDispatcher() = default;
    MessageDispatcher(const MessageDispatcher&) = delete;
    MessageDispatcher& operator=(const MessageDispatcher&) = delete;

    template <class Message, class Handler>
    [[nodiscard]] Subscription on(Handler handler) {
        static_assert(isInboundMessage<Message>, "the message is not in InboundMessage");
        return add(messageIndex<Message>, [handler = std::move(handler)](const InboundMessage& message) {
            handler(*std::get_if<Message>(&message));
        });
    }

    template <class Owner, class Message>
    [[nodiscard]] Subscription on(Owner* owner, void (Owner::*method)(const Message&)) {
        return on<Message>([owner, method](const Message& message) { (owner->*method)(message); });
    }

    template <class Handler>
    [[nodiscard]] Subscription otherwise(Handler handler) {
        return add(kInboundMessageCount, std::function<void(const InboundMessage&)>(std::move(handler)));
    }

    bool dispatch(const InboundMessage& message) {
        const std::size_t index = message.index();
        if (index >= kInboundMessageCount) {
            return false;
        }
        const std::shared_ptr<detail::DispatchTable> table = table_;
        ++table->depth;
        const bool handled = table->run(index, message);
        if (!handled) {
            table->run(kInboundMessageCount, message);
        }
        if (--table->depth == 0 && table->dirty) {
            table->compact();
        }
        return handled;
    }

private:
    Subscription add(std::size_t slot, std::function<void(const InboundMessage&)> call) {
        const std::uint64_t id = table_->nextId++;
        table_->slots[slot].push_back({id, std::move(call)});
        return Subscription(table_, slot, id);
    }

    std::shared_ptr<detail::DispatchTable> table_ = std::make_shared<detail::DispatchTable>();
};

} // namespace vibe::net

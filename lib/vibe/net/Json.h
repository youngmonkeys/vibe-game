#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace vibe::net {

class Json {
public:
    class const_iterator {
    public:
        const_iterator(const Json* parent, std::size_t index) : parent_(parent), index_(index) {}

        Json operator*() const;
        const_iterator& operator++() {
            ++index_;
            return *this;
        }
        bool operator==(const const_iterator& other) const { return index_ == other.index_; }
        bool operator!=(const const_iterator& other) const { return index_ != other.index_; }

    private:
        const Json* parent_;
        std::size_t index_;
    };

    Json() = default;

    static Json parse(std::string_view text);

    explicit operator bool() const { return node_ != nullptr; }
    const Json& operator*() const { return *this; }
    const Json* operator->() const { return this; }

    bool is_discarded() const;
    bool is_null() const;
    bool is_object() const;
    bool is_array() const;
    bool is_string() const;
    bool is_boolean() const;
    bool is_number() const;
    bool is_number_integer() const;
    bool is_number_unsigned() const;
    bool is_number_float() const;

    std::size_t size() const;
    bool empty() const;
    Json operator[](std::size_t index) const;
    Json front() const { return (*this)[0]; }
    Json member(const char* key) const;
    std::vector<std::pair<std::string, Json>> items() const;

    template <class T>
    T get() const;
    template <class T>
    T get_ref() const;

    std::string dump() const;

    const_iterator begin() const { return const_iterator(this, 0); }
    const_iterator end() const;

private:
    Json(std::shared_ptr<const void> owner, const void* node) : owner_(std::move(owner)), node_(node) {}
    Json child(const void* node) const { return Json(owner_, node); }
    Json childAt(std::size_t index) const;

    std::shared_ptr<const void> owner_;
    const void* node_ = nullptr;
};

template <>
bool Json::get<bool>() const;
template <>
std::int64_t Json::get<std::int64_t>() const;
template <>
std::uint64_t Json::get<std::uint64_t>() const;
template <>
double Json::get<double>() const;
template <>
const std::string& Json::get_ref<const std::string&>() const;

} // namespace vibe::net

#include "vibe/net/Json.h"

#include <iterator>
#include <nlohmann/json.hpp>

namespace vibe::net {
namespace {

using Raw = nlohmann::json;

const Raw* raw(const void* node) {
    return static_cast<const Raw*>(node);
}

const std::string& emptyString() {
    static const std::string kEmpty;
    return kEmpty;
}

} // namespace

Json Json::const_iterator::operator*() const {
    return parent_->childAt(index_);
}

Json Json::parse(std::string_view text) {
    auto document = std::make_shared<Raw>(Raw::parse(text.begin(), text.end(), nullptr, false));
    const void* node = document.get();
    return Json(std::move(document), node);
}

bool Json::is_discarded() const {
    return node_ && raw(node_)->is_discarded();
}

bool Json::is_null() const {
    return !node_ || raw(node_)->is_null();
}

bool Json::is_object() const {
    return node_ && raw(node_)->is_object();
}

bool Json::is_array() const {
    return node_ && raw(node_)->is_array();
}

bool Json::is_string() const {
    return node_ && raw(node_)->is_string();
}

bool Json::is_boolean() const {
    return node_ && raw(node_)->is_boolean();
}

bool Json::is_number() const {
    return node_ && raw(node_)->is_number();
}

bool Json::is_number_integer() const {
    return node_ && raw(node_)->is_number_integer();
}

bool Json::is_number_unsigned() const {
    return node_ && raw(node_)->is_number_unsigned();
}

bool Json::is_number_float() const {
    return node_ && raw(node_)->is_number_float();
}

std::size_t Json::size() const {
    return node_ ? raw(node_)->size() : 0;
}

Json::const_iterator Json::end() const {
    return const_iterator(this, is_array() || is_object() ? raw(node_)->size() : 0);
}

bool Json::empty() const {
    return size() == 0;
}

Json Json::operator[](std::size_t index) const {
    if (!is_array() || index >= raw(node_)->size()) {
        return Json();
    }
    return child(&(*raw(node_))[index]);
}

Json Json::childAt(std::size_t index) const {
    if (is_array()) {
        return (*this)[index];
    }
    if (!is_object() || index >= raw(node_)->size()) {
        return Json();
    }
    return child(&*std::next(raw(node_)->begin(), static_cast<std::ptrdiff_t>(index)));
}

Json Json::member(const char* key) const {
    if (!is_object()) {
        return Json();
    }
    const auto it = raw(node_)->find(key);
    return it == raw(node_)->end() ? Json() : child(&*it);
}

std::vector<std::pair<std::string, Json>> Json::items() const {
    std::vector<std::pair<std::string, Json>> out;
    if (!is_object()) {
        return out;
    }
    out.reserve(raw(node_)->size());
    for (auto it = raw(node_)->begin(); it != raw(node_)->end(); ++it) {
        out.emplace_back(it.key(), child(&*it));
    }
    return out;
}

template <>
bool Json::get<bool>() const {
    return is_boolean() && raw(node_)->get<bool>();
}

template <>
std::int64_t Json::get<std::int64_t>() const {
    return is_number() ? raw(node_)->get<std::int64_t>() : 0;
}

template <>
std::uint64_t Json::get<std::uint64_t>() const {
    return is_number() ? raw(node_)->get<std::uint64_t>() : 0;
}

template <>
double Json::get<double>() const {
    return is_number() ? raw(node_)->get<double>() : 0.0;
}

template <>
const std::string& Json::get_ref<const std::string&>() const {
    return is_string() ? raw(node_)->get_ref<const std::string&>() : emptyString();
}

std::string Json::dump() const {
    return node_ ? raw(node_)->dump(-1, ' ', false, Raw::error_handler_t::replace) : std::string("null");
}

} // namespace vibe::net

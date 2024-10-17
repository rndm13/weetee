#pragma once

#include <algorithm>
#include <filesystem>
#include <optional>
#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include <httplib.h>

#include "imgui.h"

#include "cassert"
#include "cmath"

#include "future"
#include "string"
#include "variant"
#include "vector"

using std::to_string;

#ifndef WIN32
#define FS_SLASH "/"
#else
#define FS_SLASH "\\"
#endif

template <typename R>
bool is_ready(std::future<R> const& f) {
    assert(f.valid());

    return f.wait_until(0) == std::future_status::ready;
}

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

#define GETTER_VISITOR(property)                                                                   \
    struct GETVISITOR##property {                                                                  \
        constexpr const auto& operator()(const auto& visitee) const {                              \
            return visitee.property;                                                               \
        }                                                                                          \
                                                                                                   \
        auto& operator()(auto& visitee) const {                                                    \
            return visitee.property;                                                               \
        }                                                                                          \
    };

#define COPY_GETTER_VISITOR(property, name)                                                        \
    struct COPYGETVISITOR##name {                                                                  \
        constexpr auto operator()(const auto& visitee) const {                                     \
            return visitee.property;                                                               \
        }                                                                                          \
    };

#define SETTER_VISITOR(property, type)                                                             \
    struct SETVISITOR##property {                                                                  \
        const type new_property;                                                                   \
        type operator()(auto& visitee) const {                                                     \
            return visitee.property = this->new_property;                                          \
        }                                                                                          \
    };

using EmptyVisitor = COPY_GETTER_VISITOR(empty(), empty);

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

template <class T>
constexpr bool operator==(const std::vector<T>& a, const std::vector<T>& b) {
    // Comparison may theoretically throw
    if (a.size() != b.size()) {
        return false;
    }

    for (size_t i = 0; i < a.size(); i++) {
        if (a[i] != b[i]) {
            return false;
        }
    }

    return true;
}

template <class T>
constexpr bool operator!=(const std::vector<T>& a, const std::vector<T>& b) {
    return !(a == b);
}

constexpr ImVec4 rgb_to_ImVec4(int r, int g, int b, int a) {
    return ImVec4(
        static_cast<float>(r) / 255.0f,
        static_cast<float>(g) / 255.0f,
        static_cast<float>(b) / 255.0f,
        static_cast<float>(a) / 255.0f);
}

constexpr void
str_find_and_replace(std::string& str, const std::string& to_replace, const std::string& replace_with) {
    size_t found = str.find(to_replace);

    while (found != std::string::npos) {
        // May throw std::length_error
        str.replace(found, to_replace.size(), replace_with);

        found = str.find(to_replace);
    }
}

// Case insensitive string comparison
constexpr bool str_contains(const std::string& haystack, const std::string& needle) {
    size_t need_idx = 0;

    for (size_t idx = 0; idx < haystack.size(); idx++) {
        if (std::tolower(haystack[idx]) == std::tolower(needle[need_idx])) {
            need_idx++;
        } else {
            need_idx = 0;
        }

        if (need_idx >= needle.size()) {
            return true;
        }
    }

    return false;
}

template <class Variant>
constexpr bool valid_variant_from_index(size_t index) {
    return index < std::variant_size_v<Variant>;
}

template <class Variant, size_t I = 0>
constexpr std::optional<Variant> variant_from_index(size_t index) {
    if constexpr (valid_variant_from_index<Variant>(I)) {
        return index == 0 ? Variant{std::in_place_index<I>}
                          : variant_from_index<Variant, I + 1>(index - 1);
    }

    return std::nullopt;
}

template <class... Args>
struct variant_cast_proxy {
    std::variant<Args...> v;

    template <class... ToArgs>
    operator std::variant<ToArgs...>() const {
        return std::visit([](auto&& arg) -> std::variant<ToArgs...> { return arg; }, v);
    }
};

template <class... Args>
auto variant_cast(const std::variant<Args...>& v) -> variant_cast_proxy<Args...> {
    return {v};
}

// Includes extension
constexpr std::string get_filename(const std::string& path) {
    size_t slash = path.rfind(FS_SLASH);

    if (slash == std::string::npos) {
        slash = 0;
    } else {
        slash += 1; // skip over it
    }
    return path.substr(slash);
}

// Doesn't include extension
constexpr std::string get_filename_noext(const std::string& path) {
    std::string filename = get_filename(path);

    size_t name_end = filename.rfind('.');
    return filename.substr(0, name_end);
}

struct SplitStringIterator {
    std::string_view str;
    std::string_view separator;

    size_t idx = 0;

    SplitStringIterator(const std::string& str, const std::string& separator) 
        : str(str), separator(separator) {}

    constexpr std::string_view next() {
        assert(this->has_next());

        size_t next = str.find(separator, this->idx);

        std::string_view result = this->str.substr(this->idx, next - this->idx);

        this->idx = std::max(next, next + this->separator.size());

        return result;
    }

    constexpr bool has_next() {
        return this->idx <= this->str.size();
    }
};

constexpr std::string_view sv_trim(std::string_view sv) {
    size_t begin_idx = 0;
    while (std::isspace(sv[begin_idx]) && begin_idx < sv.size()) {
        begin_idx++;
    } 

    size_t size = 0;
    while (!std::isspace(sv[begin_idx + size]) && begin_idx + size < sv.size()) {
        size++;
    } 

    return sv.substr(begin_idx, size);
}

std::vector<std::string> split_string(const std::string& str, const std::string& separator);

struct Range {
    size_t idx;
    size_t size;
};

constexpr bool operator==(const Range& a, const Range& b) noexcept {
    return a.idx == b.idx && a.size == b.size;
}

constexpr bool operator!=(const Range& a, const Range& b) noexcept {
    return a.idx != b.idx || a.size != b.size;
}

std::vector<Range> encapsulation_ranges(std::string str, char begin, char end);

template <typename Key, typename Value>
using MapIterator = typename std::unordered_map<Key, Value>::iterator;

template <typename Key, typename Value>
struct MapKeyIterator : public MapIterator<Key, Value> {
public:
    Key* operator->() {
        return reinterpret_cast<Key* const>(&MapIterator<Key, Value>::operator->()->first);
    }
    Key operator*() {
        return MapIterator<Key, Value>::operator*().first;
    }

    MapKeyIterator() : MapIterator<Key, Value>(){};
    MapKeyIterator(MapIterator<Key, Value> it_) : MapIterator<Key, Value>(it_){};
};

template <typename T>
struct copy_atomic {
    std::atomic<T> value;

    constexpr T load() const {
        return this->value.load();
    }

    constexpr void store(const T& new_value) {
        return this->value.store(new_value);
    }

    copy_atomic() : value() {}
    copy_atomic(const T& _value) : value(_value) {}
    copy_atomic(const std::atomic<T>& a) : value(a.load()) {}
    copy_atomic(const copy_atomic& other) : value(other.value.load()) {}
    copy_atomic& operator=(const copy_atomic& other) {
        value.store(other.value.load());
        return *this;
    }
};

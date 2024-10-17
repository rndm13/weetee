#pragma once

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

template <typename R> bool is_ready(std::future<R> const& f) {
    assert(f.valid());

    return f.wait_until(0) == std::future_status::ready;
}

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

#define GETTER_VISITOR(property)                                                                   \
    struct GETVISITOR##property {                                                                  \
        constexpr const auto& operator()(const auto& visitee) const {                     \
            return visitee.property;                                                               \
        }                                                                                          \
                                                                                                   \
        auto& operator()(auto& visitee) const { return visitee.property; }                \
    };

#define COPY_GETTER_VISITOR(property, name)                                                        \
    struct COPYGETVISITOR##name {                                                                  \
        constexpr auto operator()(const auto& visitee) const { return visitee.property; } \
    };

#define SETTER_VISITOR(property, type)                                                             \
    struct SETVISITOR##property {                                                                  \
        const type new_property;                                                                   \
        type operator()(auto& visitee) const {                                            \
            return visitee.property = this->new_property;                                          \
        }                                                                                          \
    };

using EmptyVisitor = COPY_GETTER_VISITOR(empty(), empty);

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

template <class T>
constexpr bool operator==(const std::vector<T>& a, const std::vector<T>& b) {
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
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

void find_and_replace(std::string& str, const std::string& to_replace,
                      const std::string& replace_with);

// Case insensitive string comparison
bool str_contains(const std::string& haystack, const std::string& needle);

template <class Variant> constexpr bool valid_variant_from_index(size_t index) {
    return index < std::variant_size_v<Variant>;
}

template <class Variant, size_t I = 0> constexpr Variant variant_from_index(size_t index) {
    if constexpr (valid_variant_from_index<Variant>(I)) {
        return index == 0 ? Variant{std::in_place_index<I>}
                          : variant_from_index<Variant, I + 1>(index - 1);
    }
    assert(false && "Invalid variant index");
    return Variant{};
}

template <class... Args> struct variant_cast_proxy {
    std::variant<Args...> v;

    template <class... ToArgs> operator std::variant<ToArgs...>() const {
        return std::visit([](auto&& arg) -> std::variant<ToArgs...> { return arg; }, v);
    }
};

template <class... Args>
auto variant_cast(const std::variant<Args...>& v) -> variant_cast_proxy<Args...> {
    return {v};
}

// Includes extension
std::string get_full_filename(const std::string& path);

// Doesn't include extension
std::string get_filename(const std::string& path);

std::vector<std::string> split_string(const std::string& str,
                                      const std::string& separator);

std::vector<std::pair<size_t, size_t>> encapsulation_ranges(std::string str, char begin,
                                                            char end);

template <typename Key, typename Value>
using MapIterator = typename std::unordered_map<Key, Value>::iterator;

template <typename Key, typename Value> class MapKeyIterator : public MapIterator<Key, Value> {
  public:
    MapKeyIterator() : MapIterator<Key, Value>(){};
    MapKeyIterator(MapIterator<Key, Value> it_) : MapIterator<Key, Value>(it_){};

    Key* operator->() {
        return reinterpret_cast<Key* const>(&MapIterator<Key, Value>::operator->()->first);
    }
    Key operator*() { return MapIterator<Key, Value>::operator*().first; }
};

template <typename T> struct copy_atomic {
    std::atomic<T> value;

    copy_atomic() : value() {}
    copy_atomic(const T& _value) : value(_value) {}

    copy_atomic(const std::atomic<T>& a) : value(a.load()) {}
    copy_atomic(const copy_atomic& other) : value(other.value.load()) {}
    copy_atomic& operator=(const copy_atomic& other) {
        value.store(other.value.load());
        return *this;
    }

    T load() const { return this->value.load(); }
    void store(const T& new_value) { return this->value.store(new_value); }
};

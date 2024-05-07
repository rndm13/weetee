#pragma once

#ifdef WIN32
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#endif
#include <httplib.h>

#include "hello_imgui/hello_imgui_logger.h"

#include "imgui.h"

#include "cassert"
#include "cmath"
#include "future"
#include "string"
#include "variant"
#include "vector"

#include <chrono>

using HelloImGui::Log;
using HelloImGui::LogLevel;

using std::to_string;

template <typename R> bool is_ready(std::future<R> const& f) noexcept {
    assert(f.valid());
    return f.wait_until(0) == std::future_status::ready;
}

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

#define GETTER_VISITOR(property)                                                                   \
    struct GETVISITOR##property {                                                                  \
        constexpr const auto& operator()(const auto& visitee) const noexcept {                     \
            return visitee.property;                                                               \
        }                                                                                          \
                                                                                                   \
        auto& operator()(auto& visitee) const noexcept { return visitee.property; }                \
    };

#define COPY_GETTER_VISITOR(property, name)                                                        \
    struct COPYGETVISITOR##name {                                                                  \
        constexpr auto operator()(const auto& visitee) const noexcept { return visitee.property; } \
    };

#define SETTER_VISITOR(property, type)                                                             \
    struct SETVISITOR##property {                                                                  \
        const type new_property;                                                                   \
        type operator()(auto& visitee) const noexcept {                                            \
            return visitee.property = this->new_property;                                          \
        }                                                                                          \
    };

#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

using ClientSettingsVisitor = COPY_GETTER_VISITOR(cli_settings, cli_settings)

using IDVisitor = COPY_GETTER_VISITOR(id, id)
using SetIDVisitor = SETTER_VISITOR(id, size_t)

using ParentIDVisitor = COPY_GETTER_VISITOR(parent_id, parent_id)
using SetParentIDVisitor = SETTER_VISITOR(parent_id, size_t)

using VariablesVisitor = GETTER_VISITOR(variables)

using LabelVisitor = COPY_GETTER_VISITOR(label(), label)
using EmptyVisitor = COPY_GETTER_VISITOR(empty(), empty)

template <class T>
constexpr bool operator==(const std::vector<T>& a, const std::vector<T>& b) noexcept {
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

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV | ImGuiTableFlags_Hideable |
    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

static constexpr ImGuiSelectableFlags SELECTABLE_FLAGS = ImGuiSelectableFlags_SpanAllColumns |
                                                         ImGuiSelectableFlags_AllowOverlap |
                                                         ImGuiSelectableFlags_AllowDoubleClick;

static constexpr ImGuiDragDropFlags DRAG_SOURCE_FLAGS =
    ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

bool arrow(const char* label, ImGuiDir dir) noexcept;
void remove_arrow_offset() noexcept;

constexpr ImVec4 rgb_to_ImVec4(int r, int g, int b, int a) noexcept {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

// Case insensitive string comparison
bool contains(const std::string& haystack, const std::string& needle) noexcept;

template <class Variant> constexpr bool valid_variant_from_index(size_t index) noexcept {
    return index < std::variant_size_v<Variant>;
}

template <class Variant, size_t I = 0> constexpr Variant variant_from_index(size_t index) noexcept {
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

std::string file_name(const std::string& path) noexcept;
std::vector<std::string> split_string(const std::string& str,
                                      const std::string& separator) noexcept;

std::vector<std::pair<size_t, size_t>> encapsulation_ranges(std::string str, char begin,
                                                            char end) noexcept;

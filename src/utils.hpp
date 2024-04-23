#pragma once

#include "hello_imgui/hello_imgui_logger.h"
#include "imgui.h"

#include "cassert"
#include "cmath"
#include "future"
#include "string"
#include "vector"

using HelloImGui::Log;
using HelloImGui::LogLevel;

using std::to_string;

template <typename R> bool is_ready(std::future<R> const& f) noexcept {
    assert(f.valid());
    return f.wait_until(std::chrono::system_clock::time_point::min()) == std::future_status::ready;
}

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

#define GETTER_VISITOR(property)                                                                   \
    struct {                                                                                       \
        constexpr const auto& operator()(const auto& visitee) const noexcept {                     \
            return visitee.property;                                                               \
        }                                                                                          \
                                                                                                   \
        auto& operator()(auto& visitee) const noexcept { return visitee.property; }                \
    };

#define COPY_GETTER_VISITOR(property)                                                              \
    struct {                                                                                       \
        constexpr auto operator()(const auto& visitee) const noexcept { return visitee.property; } \
    };

#define SETTER_VISITOR(property, type)                                                             \
    struct {                                                                                       \
        const type new_property;                                                                   \
        type operator()(auto& visitee) const noexcept {                                            \
            return visitee.property = this->new_property;                                          \
        }                                                                                          \
    };
#define ARRAY_SIZE(arr) (sizeof((arr)) / sizeof(*(arr)))

using ClientSettingsVisitor = COPY_GETTER_VISITOR(cli_settings);

using IDVisitor = COPY_GETTER_VISITOR(id);
using SetIDVisitor = SETTER_VISITOR(id, size_t);

using ParentIDVisitor = COPY_GETTER_VISITOR(parent_id);
using SetParentIDVisitor = SETTER_VISITOR(parent_id, size_t);

using LabelVisitor = COPY_GETTER_VISITOR(label());
using EmptyVisitor = COPY_GETTER_VISITOR(empty());

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

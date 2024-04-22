#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "immapp/immapp.h"

#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#include "BS_thread_pool.hpp"
#include "httplib.h"

#include "algorithm"
#include "cmath"
#include "cstdint"
#include "fstream"
#include "future"
#include "iterator"
#include "optional"
#include "sstream"
#include "string"
#include "type_traits"
#include "unordered_map"
#include "utility"
#include "variant"

#include "json.hpp"
#include "textinputcombo.hpp"

#ifndef NDEBUG
// show test id and parent id in tree view
// #define LABEL_SHOW_ID
#endif

// TODO: swagger file import/export
// TODO: fix progress bar for individual tests
// TODO: implement file sending
// TODO: implement variables for groups with substitution

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

bool arrow(const char* label, ImGuiDir dir) noexcept {
    assert(label);
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_Border, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
    bool result = ImGui::ArrowButton(label, dir);
    ImGui::PopStyleColor(5);
    return result;
}

void remove_arrow_offset() noexcept { ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 8); }

constexpr ImVec4 rgb_to_ImVec4(int r, int g, int b, int a) noexcept {
    return ImVec4(static_cast<float>(r) / 255.0f, static_cast<float>(g) / 255.0f,
                  static_cast<float>(b) / 255.0f, static_cast<float>(a) / 255.0f);
}

// case insensitive string comparison
bool contains(const std::string& haystack, const std::string& needle) noexcept {
    size_t need_idx = 0;
    for (char hay : haystack) {
        if (std::tolower(hay) == std::tolower(needle[need_idx])) {
            need_idx++;
        }
        if (need_idx >= needle.size()) {
            return true;
        }
    }
    return false;
}

template <typename Data> struct PartialDictElement {
    bool enabled = true;

    // do not save
    bool selected = false;
    bool to_delete = false;

    // save
    std::string key;
    Data data;

    // do not save
    std::optional<ComboFilterState> cfs; // if hints are given

    bool operator!=(const PartialDictElement<Data>& other) const noexcept {
        return this->data != other.data;
    }
};
template <typename Data> struct PartialDict {
    using DataType = Data;
    using ElementType = PartialDictElement<Data>;
    using SizeType = std::vector<PartialDictElement<Data>>::size_type;
    std::vector<PartialDictElement<Data>> elements;

    // do not save
    PartialDictElement<Data> add_element;

    bool operator==(const PartialDict& other) const noexcept {
        return this->elements == other.elements;
    }

    constexpr bool empty() const noexcept { return this->elements.empty(); }
};

template <class Variant> constexpr bool valid_variant_from_index(size_t index) noexcept {
    return index < std::variant_size_v<Variant>;
}
template <class Variant, size_t I = 0> constexpr Variant variant_from_index(size_t index) noexcept {
    if constexpr (valid_variant_from_index<Variant>(I)) {
        return index == 0 ? Variant{std::in_place_index<I>}
                          : variant_from_index<Variant, I + 1>(index - 1);
    }
    assert(false && "Invalid variant index");
}

struct SaveState {
    size_t original_size = {};
    size_t load_idx = {};
    std::vector<char> original_buffer;

    // helpers
    template <class T = void> void save(const char* ptr, size_t size = sizeof(T)) noexcept {
        assert(ptr);
        assert(size > 0);
        std::copy(ptr, ptr + size, std::back_inserter(this->original_buffer));
    }

    bool can_offset(size_t offset = 0) noexcept {
        return this->load_idx + offset <= original_buffer.size();
    }

    // modifies index, should be called before load and then reset
    template <class T = void> bool can_load(const char* ptr, size_t size = sizeof(T)) noexcept {
        assert(ptr);
        assert(size > 0);
        if (!can_offset(size)) {
            return false;
        }

        this->load_idx += size;
        return true;
    }

    char* load_offset(size_t offset = 0) noexcept {
        assert(this->load_idx + offset <= original_buffer.size());
        return original_buffer.data() + this->load_idx + offset;
    }

    template <class T = void> void load(char* ptr, size_t size = sizeof(T)) noexcept {
        assert(ptr);
        assert(size > 0);

        std::copy(this->load_offset(), this->load_offset(size), ptr);
        this->load_idx += size;
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void save(T trivial) noexcept {
        this->save<T>(reinterpret_cast<char*>(&trivial));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    bool can_load(const T& trivial) noexcept {
        return this->can_load(reinterpret_cast<const char*>(&trivial), sizeof(T));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void load(T& trivial) noexcept {
        this->load(reinterpret_cast<char*>(&trivial), sizeof(T));
    }

    void save(const std::string& str) noexcept {
        if (str.length() > 0) { // To avoid failing 0 size assertion in save
            this->save(str.data(), str.length());
        }
        this->save('\0');
    }

    bool can_load(const std::string& str) noexcept {
        size_t length = 0;
        while (this->can_offset(length) && *this->load_offset(length) != char(0)) {
            length++;
        }
        if (!this->can_offset(length)) {
            return false;
        }

        this->load_idx += length + 1; // Skip over null terminator
        return true;
    }

    void load(std::string& str) noexcept {
        size_t length = 0;
        while (*this->load_offset(length) != char(0)) {
            length++;
        }

        if (length > 0) { // To avoid failing 0 size assertion in load
            str.resize(length);
            this->load(str.data(), length);
        }

        this->load_idx++; // Skip over null terminator
    }

    template <class T> void save(const std::optional<T>& opt) noexcept {
        bool has_value = opt.has_value();
        this->save(has_value);

        if (has_value) {
            this->save(opt.value());
        }
    }

    template <class T> bool can_load(const std::optional<T>& opt) noexcept {
        bool has_value;
        this->load(has_value);

        return !has_value || this->can_load(opt.value());
    }

    template <class T> void load(std::optional<T>& opt) noexcept {
        bool has_value;
        this->load(has_value);

        if (has_value) {
            opt = T{};
            this->load(opt.value());
        } else {
            opt = std::nullopt;
        }
    }

    template <class K, class V> void save(const std::unordered_map<K, V>& map) noexcept {
        size_t size = map.size();
        this->save(size);

        for (const auto& [k, v] : map) {
            this->save(k);
            this->save(v);
        }
    }

    template <class K, class V> bool can_load(const std::unordered_map<K, V>&) noexcept {
        size_t size;
        this->load(size);

        for (size_t i = 0; i < size; i++) {
            K k = {};
            V v = {};
            if (!this->can_load(k)) {
                return false;
            }
            if (!this->can_load(v)) {
                return false;
            }
        }

        return true;
    }

    template <class K, class V> void load(std::unordered_map<K, V>& map) noexcept {
        size_t size;
        this->load(size);

        map.clear();
        for (size_t i = 0; i < size; i++) {
            K k;
            V v;
            this->load(k);
            this->load(v);
            assert(!map.contains(k));
            map.emplace(k, v);
        }
    }

    template <class... T> void save(const std::variant<T...>& variant) noexcept {
        assert(variant.index() != std::variant_npos);
        size_t index = variant.index();
        this->save(index);

        std::visit([this](const auto& s) { this->save(s); }, variant);
    }

    template <class... T> bool can_load(const std::variant<T...>& variant) noexcept {
        size_t index;
        this->load(index);
        if (index != std::variant_npos) {
            return false;
        }
        if (!valid_variant_from_index<std::variant<T...>>(index)) {
            return false;
        }
        auto var = variant_from_index<std::variant<T...>>(index);

        return std::visit([this](const auto& s) { return this->can_load(s); }, var);
    }

    template <class... T> void load(std::variant<T...>& variant) noexcept {
        size_t index;
        this->load(index);
        assert(index != std::variant_npos);

        assert(valid_variant_from_index<std::variant<T...>>(index));
        variant = variant_from_index<std::variant<T...>>(index);

        std::visit([this](auto& s) { this->load(s); }, variant);
    }

    template <class Element> void save(const std::vector<Element>& vec) noexcept {
        size_t size = vec.size();
        this->save(size);

        for (const auto& elem : vec) {
            this->save(elem);
        }
    }

    template <class Element> bool can_load(const std::vector<Element>& vec) noexcept {
        size_t size;
        this->load(size);

        for (size_t i = 0; i < size; i++) {
            Element elem = {};
            if (!this->can_load(elem)) {
                return false;
            }
        }

        return true;
    }

    template <class Element> void load(std::vector<Element>& vec) noexcept {
        size_t size;
        this->load(size);

        vec.clear();
        if (size != 0) {
            vec.reserve(size);
        }
        vec.resize(size);

        for (auto& elem : vec) {
            this->load(elem);
        }
    }

    template <class Data> void save(const PartialDict<Data>& pd) noexcept {
        this->save(pd.elements);
    }

    template <class Data> void save(const PartialDictElement<Data>& element) noexcept {
        this->save(element.enabled);
        this->save(element.key);
        this->save(element.data);
    }

    template <class Data> bool can_load(const PartialDict<Data>& pd) noexcept {
        return this->can_load(pd.elements);
    }

    template <class Data> bool can_load(const PartialDictElement<Data>& element) noexcept {
        if (!this->can_load(element.enabled)) {
            return false;
        }
        if (!this->can_load(element.key)) {
            return false;
        }
        if (!this->can_load(element.data)) {
            return false;
        }

        return true;
    }

    template <class Data> void load(PartialDict<Data>& pd) noexcept { this->load(pd.elements); }

    template <class Data> void load(PartialDictElement<Data>& element) noexcept {
        this->load(element.enabled);
        this->load(element.key);
        this->load(element.data);
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    void save(const T& any) noexcept {
        any.save(this);
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    bool can_load(const T& any) noexcept {
        return any.can_load(this);
    }

    // YOU HAVE TO BE CAREFUL NOT TO PASS POINTERS!
    template <class T>
        requires(!std::is_trivially_copyable<T>::value)
    void load(T& any) noexcept {
        any.load(this);
    }

    void finish_save() noexcept {
        this->original_size = this->original_buffer.size();
        this->original_buffer.shrink_to_fit();
    }

    void reset_load() noexcept { this->load_idx = 0; }

#define MAX_SAVE_SIZE 0x10000000
    // Returns false when failed
    bool write(std::ostream& os) const noexcept {
        assert(os);
        assert(this->original_size > 0);
        assert(this->original_buffer.size() == this->original_size);

        if (this->original_size > MAX_SAVE_SIZE) {
            return false;
        }

        os.write(reinterpret_cast<const char*>(&this->original_size), sizeof(size_t));
        os.write(this->original_buffer.data(), static_cast<int32_t>(this->original_buffer.size()));
        os.flush();

        return true;
    }

    // Returns false when failed
    bool read(std::istream& is) noexcept {
        assert(is);
        is.read(reinterpret_cast<char*>(&this->original_size), sizeof(size_t));

        assert(this->original_size > 0);
        if (this->original_size > MAX_SAVE_SIZE) {
            return false;
        }

        this->original_buffer.reserve(this->original_size);
        this->original_buffer.resize(this->original_size);

        is.read(this->original_buffer.data(), static_cast<int32_t>(this->original_size));

        return true;
    }
};

struct UndoHistory {
    size_t undo_idx = {};
    std::vector<SaveState> undo_history;

    // should be called after every edit
    template <class T> void push_undo_history(const T* obj) noexcept {
        assert(obj);

        if (this->undo_idx < this->undo_history.size() - 1) {
            // remove redos
            this->undo_history.resize(this->undo_idx);
        }

        this->undo_history.emplace_back();
        SaveState* new_save = &this->undo_history.back();
        new_save->save(*obj);
        new_save->finish_save();

        this->undo_idx = this->undo_history.size() - 1;
    }

    template <class T> void reset_undo_history(const T* obj) noexcept {
        // add initial undo
        this->undo_history.clear();
        this->undo_idx = 0;
        this->push_undo_history(obj);
    }

    constexpr bool can_undo() const noexcept { return this->undo_idx > 0; }

    template <class T> void undo(T* obj) noexcept {
        assert(this->can_undo());

        this->undo_idx--;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    constexpr bool can_redo() const noexcept {
        return this->undo_idx < this->undo_history.size() - 1;
    }

    template <class T> void redo(T* obj) noexcept {
        assert(this->can_redo());

        this->undo_idx++;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    UndoHistory() {}
    template <class T> UndoHistory(const T* obj) noexcept { reset_undo_history(obj); }

    UndoHistory(const UndoHistory&) = default;
    UndoHistory(UndoHistory&&) = default;
    UndoHistory& operator=(const UndoHistory&) = default;
    UndoHistory& operator=(UndoHistory&&) = default;
};

enum MultiPartBodyDataType : uint8_t {
    MPBD_FILES,
    MPBD_TEXT,
};

static const char* MPBDTypeLabels[] = {
    /* [MPBD_FILES] = */ reinterpret_cast<const char*>("Files"),
    /* [MPBD_TEXT] = */ reinterpret_cast<const char*>("Text"),
};

using MultiPartBodyData = std::variant<std::vector<std::string>, std::string>;
struct MultiPartBodyElementData {
    MultiPartBodyDataType type;
    MultiPartBodyData data;

    // do not save
    std::optional<pfd::open_file> open_file;

    static constexpr size_t field_count = 2;
    static const char* field_labels[field_count];

    bool operator==(const MultiPartBodyElementData& other) const noexcept {
        return this->type != other.type && this->data != other.data;
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->type);
        save->save(this->data);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->type)) {
            return false;
        }
        if (!save->can_load(this->data)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->type);
        save->load(this->data);
    }
};
const char* MultiPartBodyElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Type"),
    reinterpret_cast<const char*>("Data"),
};
using MultiPartBody = PartialDict<MultiPartBodyElementData>;
using MultiPartBodyElement = MultiPartBody::ElementType;

struct CookiesElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const CookiesElementData& other) const noexcept {
        return this->data != other.data;
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(data);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(data)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(data);
    }
};
const char* CookiesElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};
using Cookies = PartialDict<CookiesElementData>;
using CookiesElement = Cookies::ElementType;

struct ParametersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const ParametersElementData& other) const noexcept {
        return this->data != other.data;
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(data);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(data)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(data);
    }
};
const char* ParametersElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};
using Parameters = PartialDict<ParametersElementData>;
using ParametersElement = Parameters::ElementType;

struct HeadersElementData {
    std::string data;

    static constexpr size_t field_count = 1;
    static const char* field_labels[field_count];

    bool operator!=(const HeadersElementData& other) const noexcept {
        return this->data != other.data;
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(data);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(data)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(data);
    }
};
const char* HeadersElementData::field_labels[field_count] = {
    reinterpret_cast<const char*>("Data"),
};
using Headers = PartialDict<HeadersElementData>;
using HeadersElement = Headers::ElementType;

const char* RequestHeadersLabels[] = {
    reinterpret_cast<const char*>("A-IM"),
    reinterpret_cast<const char*>("Accept"),
    reinterpret_cast<const char*>("Accept-Charset"),
    reinterpret_cast<const char*>("Accept-Datetime"),
    reinterpret_cast<const char*>("Accept-Encoding"),
    reinterpret_cast<const char*>("Accept-Language"),
    reinterpret_cast<const char*>("Access-Control-Request-Method"),
    reinterpret_cast<const char*>("Access-Control-Request-Headers"),
    reinterpret_cast<const char*>("Authorization"),
    reinterpret_cast<const char*>("Cache-Control"),
    reinterpret_cast<const char*>("Connection"),
    reinterpret_cast<const char*>("Content-Encoding"),
    reinterpret_cast<const char*>("Content-Length"),
    reinterpret_cast<const char*>("Content-MD5"),
    reinterpret_cast<const char*>("Content-Type"),
    reinterpret_cast<const char*>("Cookie"),
    reinterpret_cast<const char*>("Date"),
    reinterpret_cast<const char*>("Expect"),
    reinterpret_cast<const char*>("Forwarded"),
    reinterpret_cast<const char*>("From"),
    reinterpret_cast<const char*>("Host"),
    reinterpret_cast<const char*>("HTTP2-Settings"),
    reinterpret_cast<const char*>("If-Match"),
    reinterpret_cast<const char*>("If-Modified-Since"),
    reinterpret_cast<const char*>("If-None-Match"),
    reinterpret_cast<const char*>("If-Range"),
    reinterpret_cast<const char*>("If-Unmodified-Since"),
    reinterpret_cast<const char*>("Max-Forwards"),
    reinterpret_cast<const char*>("Origin"),
    reinterpret_cast<const char*>("Pragma"),
    reinterpret_cast<const char*>("Prefer"),
    reinterpret_cast<const char*>("Proxy-Authorization"),
    reinterpret_cast<const char*>("Range"),
    reinterpret_cast<const char*>("Referer"),
    reinterpret_cast<const char*>("TE"),
    reinterpret_cast<const char*>("Trailer"),
    reinterpret_cast<const char*>("Transfer-Encoding"),
    reinterpret_cast<const char*>("User-Agent"),
    reinterpret_cast<const char*>("Upgrade"),
    reinterpret_cast<const char*>("Via"),
    reinterpret_cast<const char*>("Warning"),
};
static const char* ResponseHeadersLabels[] = {
    reinterpret_cast<const char*>("Accept-CH"),
    reinterpret_cast<const char*>("Access-Control-Allow-Origin"),
    reinterpret_cast<const char*>("Access-Control-Allow-Credentials"),
    reinterpret_cast<const char*>("Access-Control-Expose-Headers"),
    reinterpret_cast<const char*>("Access-Control-Max-Age"),
    reinterpret_cast<const char*>("Access-Control-Allow-Methods"),
    reinterpret_cast<const char*>("Access-Control-Allow-Headers"),
    reinterpret_cast<const char*>("Accept-Patch"),
    reinterpret_cast<const char*>("Accept-Ranges"),
    reinterpret_cast<const char*>("Age"),
    reinterpret_cast<const char*>("Allow"),
    reinterpret_cast<const char*>("Alt-Svc"),
    reinterpret_cast<const char*>("Cache-Control"),
    reinterpret_cast<const char*>("Connection"),
    reinterpret_cast<const char*>("Content-Disposition"),
    reinterpret_cast<const char*>("Content-Encoding"),
    reinterpret_cast<const char*>("Content-Language"),
    reinterpret_cast<const char*>("Content-Length"),
    reinterpret_cast<const char*>("Content-Location"),
    reinterpret_cast<const char*>("Content-MD5"),
    reinterpret_cast<const char*>("Content-Range"),
    reinterpret_cast<const char*>("Content-Type"),
    reinterpret_cast<const char*>("Date"),
    reinterpret_cast<const char*>("Delta-Base"),
    reinterpret_cast<const char*>("ETag"),
    reinterpret_cast<const char*>("Expires"),
    reinterpret_cast<const char*>("IM"),
    reinterpret_cast<const char*>("Last-Modified"),
    reinterpret_cast<const char*>("Link"),
    reinterpret_cast<const char*>("Location"),
    reinterpret_cast<const char*>("P3P"),
    reinterpret_cast<const char*>("Pragma"),
    reinterpret_cast<const char*>("Preference-Applied"),
    reinterpret_cast<const char*>("Proxy-Authenticate"),
    reinterpret_cast<const char*>("Public-Key-Pins"),
    reinterpret_cast<const char*>("Retry-After"),
    reinterpret_cast<const char*>("Server"),
    reinterpret_cast<const char*>("Set-Cookie"),
    reinterpret_cast<const char*>("Strict-Transport-Security"),
    reinterpret_cast<const char*>("Trailer"),
    reinterpret_cast<const char*>("Transfer-Encoding"),
    reinterpret_cast<const char*>("Tk"),
    reinterpret_cast<const char*>("Upgrade"),
    reinterpret_cast<const char*>("Vary"),
    reinterpret_cast<const char*>("Via"),
    reinterpret_cast<const char*>("Warning"),
    reinterpret_cast<const char*>("WWW-Authenticate"),
    reinterpret_cast<const char*>("X-Frame-Options"),
};

struct Test;
struct Group;
enum NestedTestType : uint8_t {
    TEST_VARIANT,
    GROUP_VARIANT,
};
using NestedTest = std::variant<Test, Group>;

enum RequestBodyType : uint8_t {
    REQUEST_JSON,
    REQUEST_PLAIN,
    REQUEST_MULTIPART,
};
static const char* RequestBodyTypeLabels[] = {
    /* [REQUEST_JSON] = */ reinterpret_cast<const char*>("JSON"),
    /* [REQUEST_RAW] = */ reinterpret_cast<const char*>("Plain Text"),
    /* [REQUEST_MULTIPART] = */ reinterpret_cast<const char*>("Multipart"),
};

using RequestBody = std::variant<std::string, MultiPartBody>;

struct Request {
    RequestBodyType body_type = REQUEST_JSON;
    RequestBody body = "";

    Cookies cookies;
    Parameters parameters;
    Headers headers;

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->body_type);
        save->save(this->body);
        save->save(this->cookies);
        save->save(this->parameters);
        save->save(this->headers);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->body_type)) {
            return false;
        }
        if (!save->can_load(this->body)) {
            return false;
        }
        if (!save->can_load(this->cookies)) {
            return false;
        }
        if (!save->can_load(this->parameters)) {
            return false;
        }
        if (!save->can_load(this->headers)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->body_type);
        save->load(this->body);
        save->load(this->cookies);
        save->load(this->parameters);
        save->load(this->headers);
    }

    constexpr bool operator==(const Request& other) const noexcept {
        return this->body_type == other.body_type && this->body == other.body &&
               this->cookies == other.cookies && this->headers == other.headers &&
               this->parameters == other.parameters;
    }
};

enum ResponseBodyType : uint8_t {
    RESPONSE_JSON,
    RESPONSE_HTML,
    RESPONSE_PLAIN,
};
static const char* ResponseBodyTypeLabels[] = {
    /* [RESPONSE_JSON] = */ reinterpret_cast<const char*>("JSON"),
    /* [RESPONSE_HTML] = */ reinterpret_cast<const char*>("HTML"),
    /* [RESPONSE_RAW] = */ reinterpret_cast<const char*>("Plain Text"),
};

using ResponseBody =
    std::variant<std::string>; // probably will need to add file responses so will keep it this way

struct Response {
    std::string status; // a string so user can get hints and write their own status code
    ResponseBodyType body_type = RESPONSE_JSON;
    ResponseBody body = "";

    Cookies cookies;
    Headers headers;

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->status);
        save->save(this->body_type);
        save->save(this->body);
        save->save(this->cookies);
        save->save(this->headers);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->status)) {
            return false;
        }
        if (!save->can_load(this->body_type)) {
            return false;
        }
        if (!save->can_load(this->body)) {
            return false;
        }
        if (!save->can_load(this->cookies)) {
            return false;
        }
        if (!save->can_load(this->headers)) {
            return false;
        }
        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->status);
        save->load(this->body_type);
        save->load(this->body);
        save->load(this->cookies);
        save->load(this->headers);
    }

    constexpr bool operator==(const Response& other) const noexcept {
        return this->status == other.status && this->body_type == other.body_type &&
               this->body == other.body && this->cookies == other.cookies &&
               this->headers == other.headers;
    }
};

enum HTTPType : uint8_t {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
};
static const char* HTTPTypeLabels[] = {
    /* [HTTP_GET] = */ reinterpret_cast<const char*>("GET"),
    /* [HTTP_POST] = */ reinterpret_cast<const char*>("POST"),
    /* [HTTP_PUT] = */ reinterpret_cast<const char*>("PUT"),
    /* [HTTP_DELETE] = */ reinterpret_cast<const char*>("DELETE"),
    /* [HTTP_PATCH] = */ reinterpret_cast<const char*>("PATCH"),
};
static ImVec4 HTTPTypeColor[] = {
    /* [HTTP_GET] = */ rgb_to_ImVec4(58, 142, 48, 255),
    /* [HTTP_POST] = */ rgb_to_ImVec4(160, 173, 64, 255),
    /* [HTTP_PUT] = */ rgb_to_ImVec4(181, 94, 65, 255),
    /* [HTTP_DELETE] = */ rgb_to_ImVec4(201, 61, 22, 255),
    /* [HTTP_PATCH] = */ rgb_to_ImVec4(99, 22, 90, 255),
};

static const char* HTTPStatusLabels[] = {
    reinterpret_cast<const char*>("100 Continue"),
    reinterpret_cast<const char*>("101 Switching Protocol"),
    reinterpret_cast<const char*>("102 Processing"),
    reinterpret_cast<const char*>("103 Early Hints"),
    reinterpret_cast<const char*>("200 OK"),
    reinterpret_cast<const char*>("201 Created"),
    reinterpret_cast<const char*>("202 Accepted"),
    reinterpret_cast<const char*>("203 Non-Authoritative Information"),
    reinterpret_cast<const char*>("204 No Content"),
    reinterpret_cast<const char*>("205 Reset Content"),
    reinterpret_cast<const char*>("206 Partial Content"),
    reinterpret_cast<const char*>("207 Multi-Status"),
    reinterpret_cast<const char*>("208 Already Reported"),
    reinterpret_cast<const char*>("226 IM Used"),
    reinterpret_cast<const char*>("300 Multiple Choices"),
    reinterpret_cast<const char*>("301 Moved Permanently"),
    reinterpret_cast<const char*>("302 Found"),
    reinterpret_cast<const char*>("303 See Other"),
    reinterpret_cast<const char*>("304 Not Modified"),
    reinterpret_cast<const char*>("305 Use Proxy"),
    reinterpret_cast<const char*>("306 unused"),
    reinterpret_cast<const char*>("307 Temporary Redirect"),
    reinterpret_cast<const char*>("308 Permanent Redirect"),
    reinterpret_cast<const char*>("400 Bad Request"),
    reinterpret_cast<const char*>("401 Unauthorized"),
    reinterpret_cast<const char*>("402 Payment Required"),
    reinterpret_cast<const char*>("403 Forbidden"),
    reinterpret_cast<const char*>("404 Not Found"),
    reinterpret_cast<const char*>("405 Method Not Allowed"),
    reinterpret_cast<const char*>("406 Not Acceptable"),
    reinterpret_cast<const char*>("407 Proxy Authentication Required"),
    reinterpret_cast<const char*>("408 Request Timeout"),
    reinterpret_cast<const char*>("409 Conflict"),
    reinterpret_cast<const char*>("410 Gone"),
    reinterpret_cast<const char*>("411 Length Required"),
    reinterpret_cast<const char*>("412 Precondition Failed"),
    reinterpret_cast<const char*>("413 Payload Too Large"),
    reinterpret_cast<const char*>("414 URI Too Long"),
    reinterpret_cast<const char*>("415 Unsupported Media Type"),
    reinterpret_cast<const char*>("416 Range Not Satisfiable"),
    reinterpret_cast<const char*>("417 Expectation Failed"),
    reinterpret_cast<const char*>("418 I'm a teapot"),
    reinterpret_cast<const char*>("421 Misdirected Request"),
    reinterpret_cast<const char*>("422 Unprocessable Content"),
    reinterpret_cast<const char*>("423 Locked"),
    reinterpret_cast<const char*>("424 Failed Dependency"),
    reinterpret_cast<const char*>("425 Too Early"),
    reinterpret_cast<const char*>("426 Upgrade Required"),
    reinterpret_cast<const char*>("428 Precondition Required"),
    reinterpret_cast<const char*>("429 Too Many Requests"),
    reinterpret_cast<const char*>("431 Request Header Fields Too Large"),
    reinterpret_cast<const char*>("451 Unavailable For Legal Reasons"),
    reinterpret_cast<const char*>("500 Internal Server Error"),
    reinterpret_cast<const char*>("501 Not Implemented"),
    reinterpret_cast<const char*>("502 Bad Gateway"),
    reinterpret_cast<const char*>("503 Service Unavailable"),
    reinterpret_cast<const char*>("504 Gateway Timeout"),
    reinterpret_cast<const char*>("505 HTTP Version Not Supported"),
    reinterpret_cast<const char*>("506 Variant Also Negotiates"),
    reinterpret_cast<const char*>("507 Insufficient Storage"),
    reinterpret_cast<const char*>("508 Loop Detected"),
    reinterpret_cast<const char*>("510 Not Extended"),
    reinterpret_cast<const char*>("511 Network Authentication Required"),
};

enum ClientSettingsFlags : uint8_t {
    CLIENT_NONE = 0,
    CLIENT_DYNAMIC = 1 << 0,
    CLIENT_KEEP_ALIVE = 1 << 1,
    CLIENT_COMPRESSION = 1 << 2,
    CLIENT_FOLLOW_REDIRECTS = 1 << 3,
};

enum CompressionType : uint8_t {
#if CPPHTTPLIB_ZLIB_SUPPORT
    COMPRESSION_ZLIB,
#endif
#if CPPHTTPLIB_BROTLI_SUPPORT
    COMPRESSION_BROTLI,
#endif
};
static const char* CompressionTypeLabels[] = {
#if CPPHTTPLIB_ZLIB_SUPPORT
    /* [COMPRESSION_ZLIB] = */ reinterpret_cast<const char*>("ZLIB"),
#endif
#if CPPHTTPLIB_BROTLI_SUPPORT
    /* [COMPRESSION_BROTLI] = */ reinterpret_cast<const char*>("Brotli"),
#endif
};

struct ClientSettings {
    uint8_t flags = CLIENT_NONE;
#if CPPHTTPLIB_ZLIB_SUPPORT || CPPHTTPLIB_BROTLI_SUPPORT
    CompressionType compression;
#endif

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->flags);
#if CPPHTTPLIB_ZLIB_SUPPORT || CPPHTTPLIB_BROTLI_SUPPORT
        save->save(this->compression);
#endif
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->flags)) {
            return false;
        }
#if CPPHTTPLIB_ZLIB_SUPPORT || CPPHTTPLIB_BROTLI_SUPPORT
        if (!save->can_load(this->compression)) {
            return false;
        }
#endif

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->flags);
#if CPPHTTPLIB_ZLIB_SUPPORT || CPPHTTPLIB_BROTLI_SUPPORT
        save->load(this->compression);
#endif
    }

    constexpr bool operator==(const ClientSettings& other) const noexcept {
        return this->flags == other.flags && this->flags & CLIENT_COMPRESSION &&
               this->compression == other.compression;
    }
};

#define CHECKBOX_FLAG(flags, changed, flag_name, flag_label)                                       \
    do {                                                                                           \
        bool flag = (flags) & (flag_name);                                                         \
        if (ImGui::Checkbox((flag_label), &flag)) {                                                \
            changed = true;                                                                        \
            if (flag) {                                                                            \
                (flags) |= (flag_name);                                                            \
            } else {                                                                               \
                (flags) &= ~(flag_name);                                                           \
            }                                                                                      \
        }                                                                                          \
    } while (0);

bool show_client_settings(ClientSettings* set) noexcept {
    assert(set);

    bool changed = false;

    CHECKBOX_FLAG(set->flags, changed, CLIENT_DYNAMIC, "Dynamic Testing");
    if (set->flags & CLIENT_DYNAMIC) {
        ImGui::SameLine();
        CHECKBOX_FLAG(set->flags, changed, CLIENT_KEEP_ALIVE, "Keep Alive Connection");
    }

#if CPPHTTPLIB_ZLIB_SUPPORT || CPPHTTPLIB_BROTLI_SUPPORT
    CHECKBOX_FLAG(set->flags, changed, CLIENT_COMPRESSION, "Enable Client Compression");
    if (set->flags & CLIENT_COMPRESSION) {
        ImGui::SameLine();
        if (ImGui::BeginCombo("##compression_type", CompressionTypeLabels[set->compression])) {
            for (size_t i = 0; i < ARRAY_SIZE(CompressionTypeLabels); i++) {
                if (ImGui::Selectable(CompressionTypeLabels[i], i == set->compression)) {
                    changed = true;
                    set->compression = static_cast<CompressionType>(i);
                }
            }
            ImGui::EndCombo();
        }
    }
#endif

    CHECKBOX_FLAG(set->flags, changed, CLIENT_FOLLOW_REDIRECTS, "Follow Redirects");
    return changed;
}
#undef CHECKBOX_FLAG

enum TestFlags : uint8_t {
    TEST_NONE = 0,
    TEST_DISABLED = 1 << 0,
};

struct Test {
    size_t parent_id;
    size_t id;
    uint8_t flags;

    HTTPType type;
    std::string endpoint;

    Request request;
    Response response;

    std::optional<ClientSettings> cli_settings;

    const std::string label() const noexcept {
#ifndef LABEL_SHOW_ID
        return this->endpoint + "##" + to_string(this->id);
#else
        return this->endpoint + "__" + to_string(this->id) + "__" + to_string(this->parent_id);
#endif
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->id);
        save->save(this->parent_id);
        save->save(this->type);
        save->save(this->flags);
        save->save(this->endpoint);
        save->save(this->request);
        save->save(this->response);
        save->save(this->cli_settings);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->id)) {
            return false;
        }
        if (!save->can_load(this->parent_id)) {
            return false;
        }
        if (!save->can_load(this->type)) {
            return false;
        }
        if (!save->can_load(this->flags)) {
            return false;
        }
        if (!save->can_load(this->endpoint)) {
            return false;
        }
        if (!save->can_load(this->request)) {
            return false;
        }
        if (!save->can_load(this->response)) {
            return false;
        }
        if (!save->can_load(this->cli_settings)) {
            return false;
        }
        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->id);
        save->load(this->parent_id);
        save->load(this->type);
        save->load(this->flags);
        save->load(this->endpoint);
        save->load(this->request);
        save->load(this->response);
        save->load(this->cli_settings);
    }
};

enum TestResultStatus {
    STATUS_RUNNING,
    STATUS_CANCELLED,
    STATUS_OK,
    STATUS_ERROR,
    STATUS_WARNING,
};
static const char* TestResultStatusLabels[] = {
    /* [STATUS_RUNNING] = */ reinterpret_cast<const char*>("Running"),
    /* [STATUS_CANCELLED] = */ reinterpret_cast<const char*>("Cancelled"),
    /* [STATUS_OK] = */ reinterpret_cast<const char*>("Ok"),
    /* [STATUS_ERROR] = */ reinterpret_cast<const char*>("Error"),
    /* [STATUS_WARNING] = */ reinterpret_cast<const char*>("Warning"),
};

struct TestResult {
    // can be written and read from any thread
    std::atomic_bool running = true;
    // main thread writes when stopping tests
    std::atomic<TestResultStatus> status = STATUS_RUNNING;

    // written in draw thread
    bool selected = true;

    // open info in a modal
    bool open;

    Test original_test;
    std::optional<httplib::Result> http_result;

    // written only in test_run threads
    std::string verdict = "0%";

    // progress
    size_t progress_total;
    size_t progress_current;

    TestResult(const Test& _original_test) noexcept : original_test(_original_test) {}
};

enum GroupFlags : uint8_t {
    GROUP_NONE = 0,
    GROUP_DISABLED = 1 << 0,
    GROUP_OPEN = 1 << 1,
};

struct Group {
    size_t parent_id;
    size_t id;
    uint8_t flags;

    std::string name;
    std::optional<ClientSettings> cli_settings;

    std::vector<size_t> children_ids;

    const std::string label() const noexcept {
#ifndef LABEL_SHOW_ID
        return this->name + "##" + to_string(this->id);
#else
        return this->name + "__" + to_string(this->id) + "__" + to_string(this->parent_id);
#endif
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        save->save(this->id);
        save->save(this->parent_id);
        save->save(this->flags);
        save->save(this->name);
        save->save(this->children_ids);
        save->save(this->cli_settings);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->id)) {
            return false;
        }
        if (!save->can_load(this->parent_id)) {
            return false;
        }
        if (!save->can_load(this->flags)) {
            return false;
        }
        if (!save->can_load(this->name)) {
            return false;
        }
        if (!save->can_load(this->children_ids)) {
            return false;
        }
        if (!save->can_load(this->cli_settings)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->id);
        save->load(this->parent_id);
        save->load(this->flags);
        save->load(this->name);
        save->load(this->children_ids);
        save->load(this->cli_settings);
    }
};

constexpr bool nested_test_eq(const NestedTest* a, const NestedTest* b) noexcept {
    if (a->index() != b->index()) {
        return false;
    }

    switch (a->index()) {
    case TEST_VARIANT: {
        assert(std::holds_alternative<Test>(*a));
        assert(std::holds_alternative<Test>(*b));

        const auto& test_a = std::get<Test>(*a);
        const auto& test_b = std::get<Test>(*b);
        return test_a.endpoint == test_b.endpoint && test_a.type == test_b.type &&
               test_a.request == test_b.request && test_a.response == test_b.response &&
               test_a.cli_settings == test_b.cli_settings;
    } break;
    case GROUP_VARIANT:
        assert(std::holds_alternative<Group>(*a));
        assert(std::holds_alternative<Group>(*b));

        const auto& group_a = std::get<Group>(*a);
        const auto& group_b = std::get<Group>(*b);
        return group_a.name == group_b.name && group_a.cli_settings == group_b.cli_settings;
        break;
    }

    // unreachable
    return false;
}

bool test_comp(const std::unordered_map<size_t, NestedTest>& tests, size_t a_id, size_t b_id) {
    assert(tests.contains(a_id));
    assert(tests.contains(b_id));

    const NestedTest& a = tests.at(a_id);
    const NestedTest& b = tests.at(b_id);

    bool group_a = std::holds_alternative<Group>(a);
    bool group_b = std::holds_alternative<Group>(b);

    if (group_a != group_b) {
        return group_a > group_b;
    }

    std::string label_a = std::visit(LabelVisitor(), a);
    std::string label_b = std::visit(LabelVisitor(), b);

    return label_a > label_b;
}

// keys are ids and values are for separate for editing (must be saved to apply changes)
// do not save
struct EditorTab {
    bool just_opened = true;
    size_t original_idx;
    std::string name;
};

struct AppState {
    // save
    size_t id_counter = 0;
    std::unordered_map<size_t, NestedTest> tests = {
        {
            0,
            Group{
                .parent_id = static_cast<size_t>(-1),
                .id = 0,
                .flags = GROUP_NONE,
                .name = "root",
                .cli_settings = ClientSettings{},
                .children_ids = {},
            },
        },
    };
    std::string tree_view_filter;
    std::unordered_set<size_t> filtered_tests = {};

    std::unordered_set<size_t> selected_tests = {};
    std::unordered_map<size_t, EditorTab> opened_editor_tabs = {};

    // keys are test ids
    std::unordered_map<size_t, TestResult> test_results;

    SaveState clipboard;
    UndoHistory undo_history;

    // don't save

    std::optional<pfd::open_file> open_file_dialog;
    std::optional<pfd::save_file> save_file_dialog;
    std::optional<std::string> filename;

    BS::thread_pool thr_pool;

    ImFont* regular_font;
    ImFont* mono_font;
    HelloImGui::RunnerParams* runner_params;

    bool tree_view_focused; // updated every frame

    bool is_running_tests() const noexcept {
        for (const auto& [id, result] : this->test_results) {
            if (result.running.load()) {
                return true;
            }
        }

        return false;
    }

    void stop_test(TestResult& result) noexcept {
        assert(result.running.load());

        result.status.store(STATUS_CANCELLED);
        result.running.store(false);
    }

    void stop_test(size_t id) noexcept {
        assert(this->test_results.contains(id));

        auto& result = this->test_results.at(id);
        this->stop_test(result);
    }

    void stop_tests() noexcept {
        for (auto& [id, result] : this->test_results) {
            if (result.running.load()) {
                this->stop_test(result);
            }
        }

        this->thr_pool.purge();
    }

    void save(SaveState* save) const noexcept {
        assert(save);

        // this save is obviously going to be pretty big so reserve instantly for speed
        save->original_buffer.reserve(4096);

        save->save(this->id_counter);
        save->save(this->tests);
    }

    bool can_load(SaveState* save) const noexcept {
        assert(save);

        if (!save->can_load(this->id_counter)) {
            return false;
        }
        if (!save->can_load(this->tests)) {
            return false;
        }

        return true;
    }

    void load(SaveState* save) noexcept {
        assert(save);

        save->load(this->id_counter);
        save->load(this->tests);
    }

    void editor_open_tab(size_t id) noexcept {
        assert(this->tests.contains(id));

        this->runner_params->dockingParams.dockableWindowOfName("Editor")->focusWindowAtNextFrame =
            true;
        if (this->opened_editor_tabs.contains(id)) {
            this->opened_editor_tabs[id].just_opened = true;
        } else {
            this->opened_editor_tabs[id] = EditorTab{
                .original_idx = id,
                .name = std::visit(LabelVisitor(), this->tests[id]),
            };
        }
    }

    void focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests) noexcept {
        assert(old_tests);

        for (auto& [id, test] : this->tests) {
            if (old_tests->contains(id) && !nested_test_eq(&test, &old_tests->at(id))) {
                this->editor_open_tab(id);
            } else {
                if (std::holds_alternative<Test>(test)) {
                    this->editor_open_tab(id);
                }
            }
        }
    }

    void post_open() noexcept {
        this->opened_editor_tabs.clear();
        this->selected_tests.clear();
        this->undo_history.reset_undo_history(this);
    }

    void post_undo() noexcept {
        for (auto it = this->opened_editor_tabs.begin(); it != this->opened_editor_tabs.end();) {
            if (!this->tests.contains(it->first)) {
                it = this->opened_editor_tabs.erase(it);
            } else {
                ++it;
            }
        }

        // remove missing tests from selected
        for (auto it = this->selected_tests.begin(); it != this->selected_tests.end();) {
            if (!this->tests.contains(*it)) {
                it = this->selected_tests.erase(it);
            } else {
                ++it;
            }
        }

        // add newly selected if parent is selected
        for (auto& [id, nt] : this->tests) {
            if (this->parent_selected(id) && !this->selected_tests.contains(id)) {
                this->select_with_children(std::visit(ParentIDVisitor(), nt));
            }
        }
    }

    void undo() noexcept {
        auto old_tests = this->tests;
        this->undo_history.undo(this);
        this->focus_diff_tests(&old_tests);
        this->post_undo();
    }

    void redo() noexcept {
        auto old_tests = this->tests;
        this->undo_history.redo(this);
        this->focus_diff_tests(&old_tests);
        this->post_undo();
    }

    bool parent_selected(size_t id) const noexcept {
        assert(this->tests.contains(id));

        return this->selected_tests.contains(std::visit(ParentIDVisitor(), this->tests.at(id)));
    }

    ClientSettings get_cli_settings(size_t id) const noexcept {
        assert(this->tests.contains(id));

        while (id != -1ull) {
            const auto& test = this->tests.at(id);
            std::optional<ClientSettings> cli = std::visit(ClientSettingsVisitor(), test);

            if (cli.has_value()) {
                return cli.value();
            }

            id = std::visit(ParentIDVisitor(), test);
        }

        assert(false && "root doesn't have client settings");
    }

    template <bool select = true> void select_with_children(size_t id) noexcept {
        assert(this->tests.contains(id));

        if constexpr (select) {
            this->selected_tests.insert(id);
        } else if (!this->parent_selected(id)) {
            this->selected_tests.erase(id);
        }

        NestedTest& nt = this->tests.at(id);

        if (!std::holds_alternative<Group>(nt)) {
            return;
        }

        Group& group = std::get<Group>(nt);

        for (size_t child_id : group.children_ids) {
            assert(this->tests.contains(child_id));
            if constexpr (select) {
                this->selected_tests.insert(child_id);
            } else if (!this->parent_selected(id)) {
                this->selected_tests.erase(child_id);
            }

            select_with_children<select>(child_id);
        }
    }

    std::vector<size_t> select_top_layer() noexcept {
        std::vector<size_t> result;
        for (auto sel_id : this->selected_tests) {
            if (!this->parent_selected(sel_id)) {
                result.push_back(sel_id);
            }
        }

        return result;
    }

    bool parent_disabled(const NestedTest* nt) noexcept {
        assert(nt);

        // OPTIM: maybe add some cache for every test that clears every frame?
        // if performance becomes a problem
        size_t id = std::visit(ParentIDVisitor(), *nt);
        while (id != -1ull) {
            assert(this->tests.contains(id));
            nt = &this->tests.at(id);

            assert(std::holds_alternative<Group>(*nt));
            const Group& group = std::get<Group>(*nt);
            if (group.flags & GROUP_DISABLED) {
                return true;
            }

            id = group.parent_id;
        }
        return false;
    }

    void move_children_up(Group* group) noexcept {
        assert(group);
        assert(group->id != 0); // not root

        auto& parent = this->tests[group->parent_id];
        assert(std::holds_alternative<Group>(parent));
        auto& parent_group = std::get<Group>(parent);

        for (auto child_id : group->children_ids) {
            assert(this->tests.contains(child_id));
            auto& child = this->tests.at(child_id);

            std::visit(SetParentIDVisitor(parent_group.id), child);
            parent_group.children_ids.push_back(child_id);
        }

        group->children_ids.clear();
    }

    void delete_children(const Group* group) noexcept {
        assert(group);
        std::vector<size_t> to_delete = group->children_ids;
        // delete_test also removes it from parent->children_idx
        // when iterating over it it creates unexpected behaviour

        for (auto child_id : to_delete) {
            this->delete_test(child_id);
        }

        assert(group->children_ids.size() <= 0); // no remaining children
    }

    void delete_test(size_t id) noexcept {
        assert(this->tests.contains(id));
        NestedTest* test = &this->tests[id];

        size_t parent_id = std::visit(ParentIDVisitor(), *test);

        if (std::holds_alternative<Group>(*test)) {
            auto& group = std::get<Group>(*test);
            this->delete_children(&group);
        }

        // remove it's id from parents child id list
        assert(this->tests.contains(parent_id));
        assert(std::holds_alternative<Group>(this->tests[parent_id]));
        auto& parent = std::get<Group>(this->tests[parent_id]);

        size_t count = std::erase(parent.children_ids, id);
        assert(count == 1);

        // remove from tests
        this->tests.erase(id);

        this->opened_editor_tabs.erase(id);
        this->selected_tests.erase(id);
    }

    void delete_selected() noexcept {
        for (auto test_id : this->select_top_layer()) {
            this->delete_test(test_id);
        }
    }

    void group_selected(size_t common_parent_id) noexcept {
        assert(this->tests.contains(common_parent_id));
        auto* parent_test = &this->tests[common_parent_id];
        assert(std::holds_alternative<Group>(*parent_test));
        auto& parent_group = std::get<Group>(*parent_test);

        // remove selected from old parent
        auto& children_idx = parent_group.children_ids;
        children_idx.erase(
            std::remove_if(children_idx.begin(), children_idx.end(), [this](size_t idx) {
                assert(this->tests.contains(idx));
                return this->selected_tests.contains(idx) && !this->parent_selected(idx);
            }));

        parent_group.flags |= GROUP_OPEN;
        auto id = ++this->id_counter;
        auto new_group = Group{
            .parent_id = parent_group.id,
            .id = id,
            .flags = GROUP_OPEN,
            .name = "New group",
            .cli_settings = {},
            .children_ids = {},
        };

        // Copy selected to new group
        std::copy_if(this->selected_tests.begin(), this->selected_tests.end(),
                     std::back_inserter(new_group.children_ids), [this](size_t id) {
                         assert(this->tests.contains(id));
                         return !this->parent_selected(id);
                     });

        // Set selected parent id to new group's id
        std::for_each(this->selected_tests.begin(), this->selected_tests.end(),
                      [this, id](size_t test_id) {
                          assert(this->tests.contains(test_id));
                          std::visit(SetParentIDVisitor(id), this->tests.at(test_id));
                      });

        // Add new group to original parent's children
        parent_group.children_ids.push_back(id);

        this->tests.emplace(id, new_group);
    }

    void copy() noexcept {
        std::unordered_map<size_t, NestedTest> to_copy;

        to_copy.reserve(this->selected_tests.size());
        for (auto sel_id : this->selected_tests) {
            assert(this->tests.contains(sel_id));

            to_copy.emplace(sel_id, this->tests.at(sel_id));
        }

        this->clipboard = {};
        this->clipboard.save(to_copy);
        this->clipboard.finish_save();
    }

    void cut() noexcept {
        this->copy();
        this->delete_selected();
    }

    constexpr bool can_paste() const noexcept { return this->clipboard.original_size > 0; }

    void paste(Group* group) noexcept {
        std::unordered_map<size_t, NestedTest> to_paste;
        this->clipboard.load(to_paste);
        this->clipboard.reset_load();

        // increments used ids
        // for tests updates id, parent children_idx (if parent present)
        // for groups should also update all children parent_id
        for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
            auto& [id, nt] = *it;

            if (!this->tests.contains(id)) {
                // if the id is free don't do anything
                continue;
            }

            size_t new_id;
            do {
                new_id = ++this->id_counter;
            } while (to_paste.contains(new_id));

            // Update parents children_idx to use new id
            size_t parent_id = std::visit(ParentIDVisitor(), nt);
            if (to_paste.contains(parent_id)) {
                auto& parent = to_paste[parent_id];
                assert(std::holds_alternative<Group>(parent));
                Group& parent_group = std::get<Group>(parent);
                auto& children_ids = parent_group.children_ids;

                size_t count = std::erase(children_ids, id);
                assert(count == 1);

                children_ids.push_back(new_id);
            }

            // Update groups children parent_id
            if (std::holds_alternative<Group>(nt)) {
                auto& group_nt = std::get<Group>(nt);
                for (size_t child_id : group_nt.children_ids) {
                    assert(to_paste.contains(child_id));
                    std::visit(SetParentIDVisitor(new_id), to_paste[child_id]);
                }
            }

            // Replace key value in map
            auto node = to_paste.extract(it);
            node.key() = new_id;
            std::visit(SetIDVisitor(new_id), node.mapped());
            to_paste.insert(std::move(node));

            // Update groups children parent_id
            if (std::holds_alternative<Group>(nt)) {
                auto& group_nt = std::get<Group>(nt);
                std::sort(group_nt.children_ids.begin(), group_nt.children_ids.end(),
                          [&to_paste](size_t a, size_t b) { return test_comp(to_paste, a, b); });
            }
        }

        // insert into passed in group
        for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
            auto& [id, nt] = *it;

            size_t parent_id = std::visit(ParentIDVisitor(), nt);
            if (!to_paste.contains(parent_id)) {
                group->children_ids.push_back(id);
                std::visit(SetParentIDVisitor(group->id), nt);
            }
        }

        group->flags |= GROUP_OPEN;
        this->tests.merge(to_paste);
        this->select_with_children(group->id);
    }

    void move(Group* group) noexcept {
        for (size_t id : this->selected_tests) {
            assert(this->tests.contains(id));

            if (this->parent_selected(id)) {
                continue;
            }

            // Remove from old parent's children
            size_t old_parent = std::visit(ParentIDVisitor(), this->tests[id]);
            assert(this->tests.contains(old_parent));
            assert(std::holds_alternative<Group>(this->tests.at(old_parent)));
            std::erase(std::get<Group>(this->tests.at(old_parent)).children_ids, id);

            // Set to new parent
            std::visit(SetParentIDVisitor(group->id), this->tests.at(id));

            group->children_ids.push_back(id);
        }

        group->flags |= GROUP_OPEN;
    }

    void sort(Group& group) noexcept {
        std::sort(group.children_ids.begin(), group.children_ids.end(),
                  [this](size_t a, size_t b) { return test_comp(this->tests, a, b); });
    }

    bool filter(Group& group) noexcept {
        bool result = true;
        for (size_t child_id : group.children_ids) {
            result &= this->filter(&this->tests[child_id]);
        }
        result &= !contains(group.name, this->tree_view_filter);

        if (result) {
            group.flags &= ~GROUP_OPEN;
        } else {
            group.flags |= GROUP_OPEN;
        }
        return result;
    }

    bool filter(Test& test) noexcept { return !contains(test.endpoint, this->tree_view_filter); }

    // returns true when the value should be filtered *OUT*
    bool filter(NestedTest* nt) noexcept {
        bool filter = std::visit([this](auto& elem) { return this->filter(elem); }, *nt);

        if (filter) {
            this->filtered_tests.insert(std::visit(IDVisitor(), *nt));
        } else {
            this->filtered_tests.erase(std::visit(IDVisitor(), *nt));
        }
        return filter;
    }

    void save_file() noexcept {
        assert(this->filename.has_value());

        std::ofstream out(this->filename.value());
        if (!out) {
            Log(LogLevel::Error, "Failed to save to file '%s'", this->filename->c_str());
            return;
        }

        SaveState save{};
        save.save(*this);
        save.finish_save();
        Log(LogLevel::Info, "Saving to '%s': %zuB", this->filename->c_str(), save.original_size);
        if (!save.write(out)) {
            Log(LogLevel::Error, "Failed to save, likely size exeeds maximum");
        }
    }

    void open_file() noexcept {
        assert(this->filename.has_value());

        std::ifstream in(this->filename.value());
        if (!in) {
            Log(LogLevel::Error, "Failed to open file \"%s\"", this->filename->c_str());
            return;
        }

        SaveState save{};
        if (!save.read(in)) {
            Log(LogLevel::Error, "Failed to read, likely file is invalid or size exeeds maximum");
            return;
        }
        Log(LogLevel::Info, "Loading from '%s': %zuB", this->filename->c_str(), save.original_size);
        if (save.can_load(*this)) {
            Log(LogLevel::Error, "Failed to load, likely file is invalid");
            return;
        }
        save.reset_load();
        save.load(*this);
        this->post_open();
    }

    AppState(HelloImGui::RunnerParams* _runner_params) noexcept : runner_params(_runner_params) {
        this->undo_history.reset_undo_history(this);
    }

    // no copy/move
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
};

struct SelectAnalysisResult {
    bool group = false;
    bool test = false;
    bool same_parent = true;
    bool selected_root = false;
    size_t parent_id = -1ull;
    size_t top_selected_count;
};

SelectAnalysisResult select_analysis(AppState* app) noexcept {
    SelectAnalysisResult result;
    result.top_selected_count = app->selected_tests.size();

    auto check_parent = [&result](size_t id) {
        if (!result.same_parent || result.selected_root) {
            return;
        }

        if (result.parent_id == -1ull) {
            result.parent_id = id;
        } else if (result.parent_id != id) {
            result.same_parent = false;
        }
    };

    for (auto test_id : app->selected_tests) {
        if (app->parent_selected(test_id)) {
            result.top_selected_count--;
            continue;
        }

        auto* selected = &app->tests[test_id];

        switch (selected->index()) {
        case TEST_VARIANT: {
            assert(std::holds_alternative<Test>(*selected));
            auto& selected_test = std::get<Test>(*selected);

            result.test = true;

            check_parent(selected_test.parent_id);
        } break;
        case GROUP_VARIANT: {
            assert(std::holds_alternative<Group>(*selected));
            auto& selected_group = std::get<Group>(*selected);

            result.group = true;
            result.selected_root |= selected_group.id == 0;

            check_parent(selected_group.parent_id);
        } break;
        }
    }

    return result;
}

bool context_menu_tree_view(AppState* app, NestedTest* nested_test) noexcept {
    bool changed = false; // this also indicates that analysis data is invalid
    size_t nested_test_id = std::visit(IDVisitor(), *nested_test);

    if (ImGui::BeginPopupContextItem()) {
        app->tree_view_focused = true;

        if (!app->selected_tests.contains(nested_test_id)) {
            app->selected_tests.clear();
            app->select_with_children(nested_test_id);
        }

        SelectAnalysisResult analysis = select_analysis(app);

        if (ImGui::MenuItem("Edit", "Enter", false, analysis.top_selected_count == 1 && !changed)) {
            app->editor_open_tab(nested_test_id);
        }

        if (ImGui::MenuItem("Delete", "Delete", false, !analysis.selected_root && !changed)) {
            changed = true;

            app->delete_selected();
        }

        if (ImGui::BeginMenu("Move", !changed && !analysis.selected_root)) {
            for (auto& [id, nt] : app->tests) {
                // skip if not a group or same parent for selected or selected group
                if (!std::holds_alternative<Group>(nt) ||
                    (analysis.same_parent && analysis.parent_id == id) ||
                    app->selected_tests.contains(id)) {
                    continue;
                }

                if (ImGui::MenuItem(std::visit(LabelVisitor(), nt).c_str(), nullptr, false,
                                    !changed)) {
                    changed = true;

                    assert(std::holds_alternative<Group>(nt));
                    app->move(&std::get<Group>(nt));
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem("Copy", "Ctrl + C", false, !changed)) {
            app->copy();
        }

        if (ImGui::MenuItem("Cut", "Ctrl + X", false, !analysis.selected_root && !changed)) {
            changed = true;

            app->cut();
        }

        // only groups without tests
        if (analysis.group && !analysis.test) {
            if (ImGui::MenuItem("Paste", "Ctrl + V", false,
                                app->can_paste() && analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);

                app->paste(&selected_group);
            }

            if (ImGui::MenuItem("Add a new test", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;

                auto id = ++app->id_counter;
                app->tests[id] = (Test{
                    .parent_id = selected_group.id,
                    .id = id,
                    .flags = TEST_NONE,
                    .type = HTTP_GET,
                    .endpoint = "https://example.com",
                    .request = {},
                    .response = {},
                    .cli_settings = {},
                });
                selected_group.children_ids.push_back(id);
                app->editor_open_tab(id);
            }

            if (ImGui::MenuItem("Add a new group", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                auto id = ++app->id_counter;
                app->tests[id] = (Group{
                    .parent_id = selected_group.id,
                    .id = id,
                    .flags = GROUP_NONE,
                    .name = "New group",
                    .cli_settings = {},
                    .children_ids = {},
                });
                selected_group.children_ids.push_back(id);
            }

            if (ImGui::MenuItem("Ungroup", nullptr, false, !analysis.selected_root && !changed)) {
                changed = true;

                for (auto selected_id : app->select_top_layer()) {
                    auto& selected = app->tests[selected_id];

                    assert(std::holds_alternative<Group>(selected));
                    auto& selected_group = std::get<Group>(selected);

                    app->move_children_up(&selected_group);
                    app->delete_test(selected_id);
                }
            }

            ImGui::Separator();
        }

        if (analysis.same_parent && ImGui::MenuItem("Group Selected", nullptr, false,
                                                    !analysis.selected_root && !changed)) {
            changed = true;

            app->group_selected(analysis.parent_id);
        }

        ImGui::EndPopup();
    }

    if (changed) {
        app->selected_tests.clear();

        app->undo_history.push_undo_history(app);
    }

    return changed;
}

bool tree_selectable(AppState* app, NestedTest& test, const char* label) noexcept {
    const auto id = std::visit(IDVisitor(), test);
    bool item_is_selected = app->selected_tests.contains(id);
    if (ImGui::Selectable(label, item_is_selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
        if (ImGui::GetIO().KeyCtrl) {
            if (item_is_selected) {
                app->select_with_children<false>(id);
            } else {
                app->select_with_children(id);
            }
        } else {
            app->selected_tests.clear();
            app->select_with_children(id);
            return true;
        }
    }
    return false;
}

bool http_type_button(HTTPType type) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HTTPTypeColor[type]);
    bool result = ImGui::SmallButton(HTTPTypeLabels[type]);
    ImGui::PopStyleColor(3);
    return result;
}

bool show_tree_view_test(AppState* app, NestedTest& test, float indentation = 0) noexcept {
    size_t id = std::visit(IDVisitor(), test);
    bool changed = false;

    if (app->filtered_tests.contains(id)) {
        return changed;
    }

    ImGui::PushID(static_cast<int32_t>(id));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);
    switch (test.index()) {
    case TEST_VARIANT: {
        auto& leaf = std::get<Test>(test);
        const auto io = ImGui::GetIO();

        ImGui::TableNextColumn(); // test
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
        http_type_button(leaf.type);
        ImGui::SameLine();
        ImGui::Text("%s", leaf.endpoint.c_str());

        ImGui::TableNextColumn(); // spinner for running tests

        if (app->test_results.contains(id) && app->test_results.at(id).running.load()) {
            ImSpinner::SpinnerIncDots("running", 5, 1);
        }

        ImGui::TableNextColumn(); // enabled / disabled

        bool pd = app->parent_disabled(&test);
        if (pd) {
            ImGui::BeginDisabled();
        }

        bool enabled = !(leaf.flags & TEST_DISABLED);
        if (ImGui::Checkbox("##enabled", &enabled)) {
            if (!enabled) {
                leaf.flags |= TEST_DISABLED;
            } else {
                leaf.flags &= ~TEST_DISABLED;
            }

            app->undo_history.push_undo_history(app);
        }

        if (pd) {
            ImGui::EndDisabled();
        }

        ImGui::TableNextColumn(); // selectable
        const bool double_clicked =
            tree_selectable(app, test, ("##" + to_string(leaf.id)).c_str()) &&
            io.MouseDoubleClicked[0];
        if (!changed && !app->selected_tests.contains(0) &&
            ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
            if (!app->selected_tests.contains(leaf.id)) {
                app->selected_tests.clear();
                app->select_with_children(leaf.id);
            }

            ImGui::Text("Moving %zu item(s)", app->selected_tests.size());
            ImGui::SetDragDropPayload("MOVE_SELECTED", &leaf.id, sizeof(size_t));
            ImGui::EndDragDropSource();
        }

        if (!app->selected_tests.contains(leaf.id) && ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("MOVE_SELECTED")) {
                changed = true;

                app->move(&std::get<Group>(app->tests[leaf.parent_id]));
            }
            ImGui::EndDragDropTarget();
        }

        changed = changed | context_menu_tree_view(app, &test);

        if (!changed && double_clicked) {
            app->editor_open_tab(leaf.id);
        }
    } break;
    case GROUP_VARIANT: {
        auto& group = std::get<Group>(test);

        ImGui::TableNextColumn(); // test
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
        if (group.flags & GROUP_OPEN) {
            arrow("down", ImGuiDir_Down);
        } else {
            arrow("right", ImGuiDir_Right);
        }
        ImGui::SameLine();
        remove_arrow_offset();
        ImGui::Text("%s", group.name.c_str());

        ImGui::TableNextColumn(); // spinner for running tests

        if (app->test_results.contains(id) && app->test_results.at(id).running.load()) {
            ImSpinner::SpinnerIncDots("running", 5, 1);
        }

        ImGui::TableNextColumn(); // enabled / disabled

        bool pd = app->parent_disabled(&test);
        if (pd) {
            ImGui::BeginDisabled();
        }

        bool enabled = !(group.flags & GROUP_DISABLED);
        if (ImGui::Checkbox("##enabled", &enabled)) {
            if (!enabled) {
                group.flags |= GROUP_DISABLED;
            } else {
                group.flags &= ~GROUP_DISABLED;
            }

            app->undo_history.push_undo_history(app);
        }

        if (pd) {
            ImGui::EndDisabled();
        }

        ImGui::TableNextColumn(); // selectable
        const bool clicked = tree_selectable(app, test, ("##" + to_string(group.id)).c_str());
        if (clicked) {
            group.flags ^= GROUP_OPEN; // toggle
        }

        if (!changed && !app->selected_tests.contains(0) &&
            ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
            if (!app->selected_tests.contains(group.id)) {
                app->selected_tests.clear();
                app->select_with_children(group.id);
            }

            ImGui::Text("Moving %zu item(s)", app->selected_tests.size());
            ImGui::SetDragDropPayload("MOVE_SELECTED", nullptr, 0);
            ImGui::EndDragDropSource();
        }

        if (!app->selected_tests.contains(group.id) && ImGui::BeginDragDropTarget()) {
            if (ImGui::AcceptDragDropPayload("MOVE_SELECTED")) {
                changed = true;

                app->move(&group);
            }
            ImGui::EndDragDropTarget();
        }

        changed |= context_menu_tree_view(app, &test);

        if (!changed && group.flags & GROUP_OPEN) {
            for (size_t child_id : group.children_ids) {
                assert(app->tests.contains(child_id));
                changed =
                    changed | show_tree_view_test(app, app->tests[child_id], indentation + 22);
                if (changed) {
                    break;
                }
            }
        }

        // changed could be that we have deleted this group too
        if (changed && app->tests.contains(id)) {
            app->sort(group);
        }
    } break;
    }

    ImGui::PopID();

    return changed;
}

void tree_view(AppState* app) noexcept {
    app->tree_view_focused = ImGui::IsWindowFocused();

    ImGui::PushFont(app->regular_font);

    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputText("##tree_view_search", &app->tree_view_filter);

    if (ImGui::BeginTable("tests", 4)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
        ImGui::TableSetupColumn("selectable", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        changed = changed | show_tree_view_test(app, app->tests[0]);
        ImGui::EndTable();
    }

    if (changed) {
        app->filter(&app->tests[0]);
    }

    ImGui::PopFont();
}

template <typename Data>
bool partial_dict_row(AppState* app, PartialDict<Data>* pd, PartialDictElement<Data>* elem,
                      const char** hints = nullptr, const size_t hint_count = 0) noexcept {
    bool changed = false;
    auto select_only_this = [pd, elem]() {
        for (auto& e : pd->elements) {
            e.selected = false;
        }
        elem->selected = true;
    };
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::Checkbox("##enabled", &elem->enabled);
        ImGui::SameLine();
        if (ImGui::Selectable("##element", elem->selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
            if (ImGui::GetIO().KeyCtrl) {
                elem->selected = !elem->selected;
            } else {
                select_only_this();
            }
        }
        if (ImGui::BeginPopupContextItem()) {
            if (!elem->selected) {
                select_only_this();
            }

            if (ImGui::MenuItem("Delete")) {
                changed = true;

                for (auto& e : pd->elements) {
                    e.to_delete = e.selected;
                }
            }

            if (ImGui::MenuItem("Enable")) {
                for (auto& e : pd->elements) {
                    e.enabled = e.enabled || e.selected;
                }
            }

            if (ImGui::MenuItem("Disable")) {
                for (auto& e : pd->elements) {
                    e.enabled = e.enabled && !e.selected;
                }
            }
            ImGui::EndPopup();
        }
    }
    if (ImGui::TableNextColumn()) { // name
        if (hint_count > 0) {
            assert(hints);
            if (!elem->cfs) {
                elem->cfs = ComboFilterState{};
            }
            ImGui::SetNextItemWidth(-1);
            changed =
                changed | ComboFilter("##name", &elem->key, hints, hint_count, &elem->cfs.value());
        } else {
            ImGui::SetNextItemWidth(-1);
            changed = changed | ImGui::InputText("##name", &elem->key);
        }
    }

    changed = changed | partial_dict_data_row(app, pd, elem);
    return changed;
}

bool partial_dict_data_row(AppState*, Cookies*, CookiesElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, Parameters*, ParametersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, Headers*, HeadersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, MultiPartBody*, MultiPartBodyElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) { // type
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##type", MPBDTypeLabels[elem->data.type])) {
            for (size_t i = 0; i < ARRAY_SIZE(MPBDTypeLabels); i++) {
                if (ImGui::Selectable(MPBDTypeLabels[i], i == elem->data.type)) {
                    elem->data.type = static_cast<MultiPartBodyDataType>(i);

                    switch (elem->data.type) {
                    case MPBD_TEXT:
                        elem->data.data = "";
                        if (elem->data.open_file.has_value()) {
                            elem->data.open_file->kill();
                            elem->data.open_file.reset();
                        }
                        break;
                    case MPBD_FILES:
                        elem->data.data = std::vector<std::string>{};
                        break;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::TableNextColumn()) { // body
        switch (elem->data.type) {
        case MPBD_TEXT:
            ImGui::SetNextItemWidth(-1);
            assert(std::holds_alternative<std::string>(elem->data.data));
            changed = changed | ImGui::InputText("##text", &std::get<std::string>(elem->data.data));
            break;
        case MPBD_FILES:
            assert(std::holds_alternative<std::vector<std::string>>(elem->data.data));
            auto& files = std::get<std::vector<std::string>>(elem->data.data);
            std::string text = files.empty() ? "Select Files"
                                             : "Selected " + to_string(files.size()) +
                                                   " Files (Hover to see names)";
            if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) {
                elem->data.open_file =
                    pfd::open_file("Select Files", ".", {"All Files", "*"}, pfd::opt::multiselect);
            }

            if (!files.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                // NOTE: could be slow to do this every frame
                std::stringstream ss;
                for (auto& file : files) {
                    ss << file << '\n';
                }
                ImGui::SetTooltip("%s", ss.str().c_str());
            }

            if (elem->data.open_file.has_value() && elem->data.open_file->ready()) {
                elem->data.data = elem->data.open_file->result();
                changed |= elem->data.open_file->result().size() > 0;
                elem->data.open_file = std::nullopt;
            }
            break;
        }
    }
    return changed;
}

template <typename Data>
bool partial_dict(AppState* app, PartialDict<Data>* pd, const char* label,
                  const char** hints = nullptr, const size_t hint_count = 0) noexcept {
    using DataType = PartialDict<Data>::DataType;

    bool changed = false;

    if (ImGui::BeginTable(label, 2 + DataType::field_count, TABLE_FLAGS, ImVec2(0, 300))) {
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("Name");
        for (size_t i = 0; i < DataType::field_count; i++) {
            ImGui::TableSetupColumn(DataType::field_labels[i]);
        }
        ImGui::TableHeadersRow();
        bool deletion = false;

        for (size_t i = 0; i < pd->elements.size(); i++) {
            auto* elem = &pd->elements[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(i));
            changed = partial_dict_row(app, pd, elem, hints, hint_count);
            deletion |= elem->to_delete;
            ImGui::PopID();
        }

        if (deletion) {
            changed = true;

            pd->elements.erase(std::remove_if(pd->elements.begin(), pd->elements.end(),
                                              [](const auto& elem) { return elem.to_delete; }));
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); // enabled, skip
        if (ImGui::TableNextColumn()) {
            ImGui::Text("Change this to add new elements");
        }

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int32_t>(pd->elements.size()));
        if (partial_dict_row(app, pd, &pd->add_element, hints, hint_count)) {
            changed = true;

            pd->elements.push_back(pd->add_element);
            pd->add_element = {};
        }
        ImGui::PopID();
        ImGui::EndTable();
    }

    return changed;
}

bool editor_test_request(AppState* app, EditorTab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Request")) {
        ImGui::PushID("request");

        if (ImGui::BeginTabItem("Request")) {
            ImGui::Text("Select any of the tabs to edit test's request");
            ImGui::Text("TODO: add a summary of request here");
            ImGui::EndTabItem();
        }

        if (test.type != HTTP_GET && ImGui::BeginTabItem("Body")) {
            if (ImGui::Combo("Body Type", reinterpret_cast<int*>(&test.request.body_type),
                             RequestBodyTypeLabels, ARRAY_SIZE(RequestBodyTypeLabels))) {
                changed = true;

                // TODO: convert between current body types
                switch (test.request.body_type) {
                case REQUEST_JSON:
                case REQUEST_PLAIN:
                    if (!std::holds_alternative<std::string>(test.request.body)) {
                        test.request.body = "";
                    }
                    break;

                case REQUEST_MULTIPART:
                    if (!std::holds_alternative<MultiPartBody>(test.request.body)) {
                        test.request.body = MultiPartBody{};
                    }
                    break;
                }
            }

            switch (test.request.body_type) {
            case REQUEST_JSON:
            case REQUEST_PLAIN:
                ImGui::PushFont(app->mono_font);
                changed = changed |
                          ImGui::InputTextMultiline(
                              "##body", &std::get<std::string>(test.request.body), ImVec2(0, 300));
                ImGui::PopFont();

                if (ImGui::BeginPopupContextItem()) {
                    if (test.request.body_type == REQUEST_JSON && ImGui::MenuItem("Format")) {
                        assert(std::holds_alternative<std::string>(test.request.body));
                        const char* error = json_format(&std::get<std::string>(test.request.body));
                        if (error) {
                            Log(LogLevel::Error, "Failed to parse json: ", error);
                        }
                    }
                    ImGui::EndPopup();
                }
                break;

            case REQUEST_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.request.body);
                changed = changed | partial_dict(app, &mpb, "##body");
                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Parameters")) {
            ImGui::Text("TODO: add undeletable params from url");
            if (!std::visit(EmptyVisitor(), test.request.body)) {
                ImGui::Text("If body is specified params non-link are disabled");
            } else {
                ImGui::PushFont(app->mono_font);
                changed = changed | partial_dict(app, &test.request.parameters, "##parameters");
                ImGui::PopFont();
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cookies")) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.request.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::PushFont(app->mono_font);
            changed =
                changed | partial_dict(app, &test.request.headers, "##headers",
                                       RequestHeadersLabels, ARRAY_SIZE(RequestHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

bool editor_test_response(AppState* app, EditorTab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Response")) {
        ImGui::PushID("response");

        if (ImGui::BeginTabItem("Response")) {
            static ComboFilterState s{};
            ComboFilter("Status", &test.response.status, HTTPStatusLabels,
                        ARRAY_SIZE(HTTPStatusLabels), &s);
            ImGui::Text("Select any of the tabs to edit test's expected response");
            ImGui::Text("TODO: add a summary of expected response here");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            bool body_type_changed = false;
            if (ImGui::BeginCombo("Body Type", ResponseBodyTypeLabels[test.response.body_type])) {
                for (size_t i = 0; i < ARRAY_SIZE(ResponseBodyTypeLabels); i++) {
                    if (ImGui::Selectable(ResponseBodyTypeLabels[i],
                                          i == test.response.body_type)) {
                        body_type_changed = true;
                        test.response.body_type = static_cast<ResponseBodyType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            if (body_type_changed) {
                changed = true;

                // TODO: convert between current body types
                switch (test.response.body_type) {
                case RESPONSE_JSON:
                case RESPONSE_HTML:
                case RESPONSE_PLAIN:
                    if (!std::holds_alternative<std::string>(test.response.body)) {
                        test.response.body = "";
                    }
                    break;
                }
            }

            switch (test.response.body_type) {
            case RESPONSE_JSON:
            case RESPONSE_HTML:
            case RESPONSE_PLAIN:
                ImGui::PushFont(app->mono_font);
                changed = changed |
                          ImGui::InputTextMultiline(
                              "##body", &std::get<std::string>(test.response.body), ImVec2(0, 300));
                ImGui::PopFont();

                if (ImGui::BeginPopupContextItem()) {
                    if (test.response.body_type == RESPONSE_JSON && ImGui::MenuItem("Format")) {
                        assert(std::holds_alternative<std::string>(test.response.body));
                        const char* error = json_format(&std::get<std::string>(test.response.body));
                        if (error) {
                            Log(LogLevel::Error, "Failed to parse json: ", error);
                        }
                    }
                    ImGui::EndPopup();
                }

                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Set Cookies")) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.response.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::PushFont(app->mono_font);
            changed =
                changed | partial_dict(app, &test.response.headers, "##headers",
                                       ResponseHeadersLabels, ARRAY_SIZE(ResponseHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

enum ModalResult : uint8_t {
    MODAL_NONE,
    MODAL_CONTINUE,
    MODAL_SAVE,
    MODAL_CANCEL,
};

ModalResult unsaved_changes(AppState*) noexcept {
    if (!ImGui::IsPopupOpen("Unsaved Changes")) {
        ImGui::OpenPopup("Unsaved Changes");
    }

    ModalResult result = MODAL_NONE;
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr)) {
        ImGui::TextColored(HTTPTypeColor[HTTP_DELETE], "WARNING");
        ImGui::Text("You are about to lose unsaved changes");

        if (ImGui::Button("Continue")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CONTINUE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_SAVE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CANCEL;
        }
        ImGui::EndPopup();
    }

    return result;
}

void show_httplib_headers(AppState* app, const httplib::Headers& headers) noexcept {
    if (ImGui::BeginTable("headers", 2, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        ImGui::PushFont(app->mono_font);
        for (const auto& [key, value] : headers) {
            ImGui::TableNextRow();
            ImGui::PushID((key + value).c_str());
            // is given readonly flag so const_cast is fine
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##key", &const_cast<std::string&>(key),
                                 ImGuiInputTextFlags_ReadOnly);
            }
            // is given readonly flag so const_cast is fine
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##value", &const_cast<std::string&>(value),
                                 ImGuiInputTextFlags_ReadOnly);
            }
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

constexpr bool is_cookie_attribute(std::string key) noexcept {
    std::for_each(key.begin(), key.end(), [](char& c) { c = static_cast<char>(std::tolower(c)); });
    return key == "domain" || key == "expires" || key == "httponly" || key == "max-age" ||
           key == "partitioned" || key == "path" || key == "samesite" || key == "secure";
}

void show_httplib_cookies(AppState* app, const httplib::Headers& headers) noexcept {
    if (ImGui::BeginTable("cookies", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn(" ");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        ImGui::PushFont(app->mono_font);
        for (const auto& [key, value] : headers) {
            if (key != "Set-Cookie") {
                continue;
            }

            ImGui::PushID(value.c_str());
            bool open = false;
            size_t search_idx = 0;
            while (search_idx != std::string::npos) {
                size_t key_val_split = value.find("=", search_idx);
                size_t next_pair = value.find(";", search_idx);
                std::string cookie_key, cookie_value;

                if (key_val_split == std::string::npos ||
                    (key_val_split > next_pair && next_pair != std::string::npos)) {
                    // Single key without value, example:
                    // A=b; Secure; HttpOnly; More=c
                    cookie_key = value.substr(search_idx, next_pair - search_idx);
                } else {
                    cookie_key = value.substr(search_idx, key_val_split - search_idx);
                    cookie_value = value.substr(key_val_split + 1, next_pair - (key_val_split + 1));
                }

                bool cookie_attribute = is_cookie_attribute(cookie_key);

                // close when new cookie starts
                if (open && !cookie_attribute) {
                    open = false;
                    ImGui::TreePop();
                    ImGui::PopID();
                }
                if (open || !cookie_attribute) {
                    ImGui::TableNextRow();

                    ImGui::PushID(static_cast<int32_t>(search_idx));
                    if (ImGui::TableNextColumn() && !cookie_attribute) {
                        open = ImGui::TreeNodeEx("##tree_node", ImGuiTreeNodeFlags_SpanFullWidth);
                    }
                    if (ImGui::TableNextColumn()) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##key", &cookie_key, ImGuiInputTextFlags_ReadOnly);
                    }
                    if (ImGui::TableNextColumn()) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##value", &cookie_value, ImGuiInputTextFlags_ReadOnly);
                    }

                    // Pop id only for cookie attributes and not open cookies
                    if (!open || cookie_attribute) {
                        ImGui::PopID();
                    }
                }

                if (next_pair == std::string::npos) {
                    break;
                }
                search_idx = next_pair + 1;                  // Skip over semicolon
                while (std::isspace(value.at(search_idx))) { // Skip over white spaces
                    search_idx += 1;
                }
            }

            // close last
            if (open) {
                open = false;
                ImGui::TreePop();
                ImGui::PopID();
            }

            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

ModalResult open_result_details(AppState* app, const TestResult* tr) noexcept {
    if (!ImGui::IsPopupOpen("Test Result Details")) {
        ImGui::OpenPopup("Test Result Details");
    }

    ModalResult result = MODAL_NONE;
    bool open = true;
    if (ImGui::BeginPopupModal("Test Result Details", &open)) {
        if (ImGui::Button("Goto original test")) {
            if (app->tests.contains(tr->original_test.id)) {
                app->editor_open_tab(tr->original_test.id);
            } else {
                Log(LogLevel::Error, "Original test is missing");
            }
        }

        ImGui::Text("%s - %s", TestResultStatusLabels[tr->status.load()], tr->verdict.c_str());

        if (tr->http_result && tr->http_result.value()) {
            const auto& http_result = tr->http_result.value();

            if (http_result.error() != httplib::Error::Success) {
                ImGui::Text("Error: %s", to_string(http_result.error()).c_str());
            } else {
                if (ImGui::BeginTabBar("Response")) {
                    if (ImGui::BeginTabItem("Body")) {
                        ImGui::Text("%d - %s", http_result->status,
                                    httplib::status_message(http_result->status));

                        ImGui::SameLine();
                        ImGui::Button("?");
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Expected: %s",
                                              tr->original_test.response.status.c_str());
                        }

                        {
                            // TODO: add a diff like view
                            ImGui::PushFont(app->mono_font);
                            // is given readonly flag so const_cast is fine
                            ImGui::InputTextMultiline(
                                "##response_body", &const_cast<std::string&>(http_result->body),
                                ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);

                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                std::string body = "*Multipart Data*";
                                if (std::holds_alternative<std::string>(
                                        tr->original_test.response.body)) {
                                    body = std::get<std::string>(tr->original_test.response.body);
                                }
                                ImGui::SetTooltip(
                                    "Expected: %s\n%s",
                                    ResponseBodyTypeLabels[tr->original_test.response.body_type],
                                    body.c_str());
                            }
                            ImGui::PopFont();
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Cookies")) {
                        show_httplib_cookies(app, http_result->headers);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Headers")) {
                        // TODO: add expected headers in split window
                        show_httplib_headers(app, http_result->headers);

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        } else {
            ImGui::Text("No response");
        }

        ImGui::EndPopup();
    }

    if (!open) {
        result = MODAL_CONTINUE;
    }

    return result;
}

enum EditorTabResult : uint8_t {
    TAB_NONE,
    TAB_CLOSED,
    TAB_CHANGED,
};

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab) noexcept {
    auto edit = &app->tests[tab.original_idx];

    assert(std::holds_alternative<Test>(*edit));
    auto& test = std::get<Test>(*edit);

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            tab.name.c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("test", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText("Endpoint", &test.endpoint);
            changed = changed | ImGui::IsItemDeactivatedAfterEdit();

            if (ImGui::BeginCombo("Type", HTTPTypeLabels[test.type])) {
                for (size_t i = 0; i < ARRAY_SIZE(HTTPTypeLabels); i++) {
                    if (ImGui::Selectable(HTTPTypeLabels[i], i == test.type)) {
                        changed = true;
                        test.type = static_cast<HTTPType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            changed = changed | editor_test_request(app, tab, test);
            changed = changed | editor_test_response(app, tab, test);

            ImGui::Text("Client Settings");
            ImGui::Separator();

            bool enable_settings = test.cli_settings.has_value() || test.parent_id == -1ull;
            if (test.parent_id != -1ull && ImGui::Checkbox("Override Parent", &enable_settings)) {
                changed = true;
                if (enable_settings) {
                    test.cli_settings = ClientSettings{};
                } else {
                    test.cli_settings = std::nullopt;
                }
            }

            if (!enable_settings) {
                ImGui::BeginDisabled();
            }

            ClientSettings cli_settings = app->get_cli_settings(test.id);
            if (show_client_settings(&cli_settings)) {
                changed = true;
                test.cli_settings = cli_settings;
            }

            if (!enable_settings) {
                ImGui::EndDisabled();
            }

            ImGui::EndChild();
        }

        ImGui::EndTabItem();
    }

    if (!open) {
        result = TAB_CLOSED;
    }

    if (changed && result == TAB_NONE) {
        result = TAB_CHANGED;
    }

    return result;
}

EditorTabResult editor_tab_group(AppState* app, EditorTab& tab) noexcept {
    auto edit = &app->tests[tab.original_idx];

    assert(std::holds_alternative<Group>(*edit));
    auto& group = std::get<Group>(*edit);

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            tab.name.c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("group", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText("Name", &group.name);
            changed |= ImGui::IsItemDeactivatedAfterEdit();

            ImGui::Text("Client Settings");
            ImGui::Separator();

            bool enable_settings = group.cli_settings.has_value() || group.parent_id == -1ull;
            if (group.parent_id != -1ull && ImGui::Checkbox("Override Parent", &enable_settings)) {
                changed = true;
                if (enable_settings) {
                    group.cli_settings = ClientSettings{};
                } else {
                    group.cli_settings = std::nullopt;
                }
            }

            if (!enable_settings) {
                ImGui::BeginDisabled();
            }

            ClientSettings cli_settings = app->get_cli_settings(group.id);
            if (show_client_settings(&cli_settings)) {
                changed = true;
                group.cli_settings = cli_settings;
            }

            if (!enable_settings) {
                ImGui::EndDisabled();
            }

            ImGui::EndChild();
        }

        ImGui::EndTabItem();
    }

    if (!open) {
        result = TAB_CLOSED;
    }

    if (changed && result == TAB_NONE) {
        result = TAB_CHANGED;
    }

    return result;
}

void tabbed_editor(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);

    if (ImGui::BeginTabBar("editor")) {
        size_t closed_id = -1ull;
        for (auto& [id, tab] : app->opened_editor_tabs) {
            NestedTest* original = &app->tests[tab.original_idx];
            EditorTabResult result;
            switch (app->tests[tab.original_idx].index()) {
            case TEST_VARIANT: {
                result = editor_tab_test(app, tab);
            } break;
            case GROUP_VARIANT: {
                result = editor_tab_group(app, tab);
            } break;
            }

            tab.just_opened = false;

            // hopefully can't close 2 tabs in a single frame
            switch (result) {
            case TAB_CLOSED:
                closed_id = id;
                break;
            case TAB_CHANGED:
                tab.name = std::visit(LabelVisitor(), *original);
                tab.just_opened = true; // to force refocus after
                app->undo_history.push_undo_history(app);
            case TAB_NONE:
                break;
            }
        }

        if (closed_id != -1ull) {
            app->opened_editor_tabs.erase(closed_id);
        }
        ImGui::EndTabBar();
    }
    ImGui::PopFont();
}

std::pair<std::string, std::string> split_endpoint(std::string endpoint) {
    size_t semicolon = endpoint.find(":");
    if (semicolon == std::string::npos) {
        semicolon = 0;
    } else {
        semicolon += 3;
    }
    size_t slash = endpoint.find("/", semicolon);
    if (slash == std::string::npos) {
        return {endpoint, "/"};
    }

    std::string host = endpoint.substr(0, slash);
    std::string dest = endpoint.substr(slash);
    return {host, dest};
}

httplib::Headers test_headers(const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->request.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, header.data.data);
    }

    std::string cookie_string;
    for (const auto& cookie : test->request.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        cookie_string += cookie.key + "=" + cookie.data.data + ";";
    }
    if (!cookie_string.empty()) {
        cookie_string.pop_back(); // remove last semicolon
        result.emplace("Cookie", cookie_string);
    }

    return result;
}

httplib::Params test_params(const Test* test) noexcept {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!param.enabled) {
            continue;
        }
        result.emplace(param.key, param.data.data);
    };

    return result;
}

std::string test_content_type(const Test* test) noexcept {
    assert(test);
    switch (test->request.body_type) {
    case REQUEST_JSON:
        return "application/json";
    case REQUEST_PLAIN:
        return "text/plain";
    case REQUEST_MULTIPART:
        return "multipart/form-data";
    }
    assert(false && "Unknown request body type");
}

httplib::Result make_request(AppState* app, const Test* test) noexcept {
    const auto params = test_params(test);
    const auto headers = test_headers(test);
    const std::string content_type = test_content_type(test);
    auto progress = [app, test](size_t current, size_t total) -> bool {
        // printf("Progress test_id: %zu, current: %zu, total: %zu\n", test->id, current, total);

        // missing
        if (!app->test_results.contains(test->id)) {
            return false;
        }

        TestResult* result = &app->test_results.at(test->id);

        // stopped
        if (!result->running.load()) {
            result->status.store(STATUS_CANCELLED);

            return false;
        }

        result->progress_total = total;
        result->progress_current = current;
        result->verdict =
            to_string(static_cast<float>(current * 100) / static_cast<float>(total)) + "% ";

        return true;
    };

    httplib::Result result;
    // Log(LogLevel::Debug, "Sending %s request to %s", HTTPTypeLabels[test->type],
    // test->label().c_str());

    auto [host, dest] = split_endpoint(test->endpoint);
    httplib::Client cli(host);

    cli.set_compress(test->cli_settings->flags & CLIENT_COMPRESSION);
    cli.set_follow_location(test->cli_settings->flags & CLIENT_FOLLOW_REDIRECTS);

    switch (test->type) {
    case HTTP_GET:
        result = cli.Get(dest, params, headers, progress);
        break;
    case HTTP_POST:
        // TODO: POST doesn't use body
        result = cli.Post(dest, headers, params, progress);
        break;
    case HTTP_PUT:
        // TODO: PUT doesn't use body
        // TODO: something goes terribly wrong here
        result = cli.Put(dest, headers, params, progress);
        break;
    case HTTP_PATCH:
        // TODO: PATCH doesn't use params
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Patch(dest, headers, body, "application/json", progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for PATCH yet");
        }
        break;
    case HTTP_DELETE:
        // TODO: figure out content types
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Delete(dest, headers, body, "application/json", progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for DELETE yet");
        }
        break;
    }
    // Log(LogLevel::Debug, "Finished %s request for %s", HTTPTypeLabels[test->type],
    // test->label().c_str());
    return result;
}

bool status_match(const std::string& match, int status) noexcept {
    auto status_str = to_string(status);
    for (size_t i = 0; i < std::min(match.size(), 3ul); i++) {
        if (std::tolower(match[i]) == 'x') {
            continue;
        }
        if (match[i] != status_str[i]) {
            return false;
        }
    }
    return true;
}

struct ContentType {
    std::string type;
    std::string name;

    constexpr bool operator!=(const ContentType& other) noexcept {
        if (other.type != this->type) {
            return true;
        }
        return other.name == this->name;
    }
};
ContentType parse_content_type(std::string input) noexcept {
    size_t slash = input.find("/");
    size_t end = input.find(";");

    std::string type = input.substr(0, slash);
    std::string name = input.substr(slash + 1, end - (slash + 1));

    return {.type = type, .name = name};
}

const char* body_match(const Test* test, const httplib::Result& result) noexcept {
    if (result->has_header("Content-Type")) {
        ContentType to_match;
        switch (test->response.body_type) {
        case RESPONSE_JSON:
            to_match = {.type = "application", .name = "json"};
            break;
        case RESPONSE_HTML:
            to_match = {.type = "text", .name = "html"};
            break;
        case RESPONSE_PLAIN:
            to_match = {.type = "text", .name = "plain"};
            break;
        }

        ContentType content_type = parse_content_type(result->get_header_value("Content-Type"));
        // printf("%s / %s = %s / %s\n", to_match.type.c_str(), to_match.name.c_str(),
        // content_type.type.c_str(), content_type.name.c_str());

        if (to_match != content_type) {
            return "Unexpected Content-Type";
        }

        if (!std::visit(EmptyVisitor(), test->response.body)) {
            if (test->response.body_type == RESPONSE_JSON) {
                assert(std::holds_alternative<std::string>(test->response.body));
                const char* err =
                    json_validate(&std::get<std::string>(test->response.body), &result->body);
                if (err) {
                    return err;
                }
            } else {
                assert(std::holds_alternative<std::string>(test->response.body));
                if (std::get<std::string>(test->response.body) != result->body) {
                    return "Unexpected Body";
                }
            }
        }
    }

    return nullptr;
}

const char* header_match(const Test* test, const httplib::Result& result) noexcept {
    httplib::Headers headers = test_headers(test);
    for (const auto& elem : test->response.cookies.elements) {
        if (elem.enabled) {
            headers.emplace("Set-Cookie", elem.key + "=" + elem.data.data);
        }
    }

    for (const auto& [key, value] : headers) {
        if (!result->has_header(key) || !contains(result->get_header_value(value), value)) {
            return "Unexpected Headers";
        }
    }

    return nullptr;
}

void test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result) noexcept {
    switch (http_result.error()) {
    case httplib::Error::Success: {
        if (!status_match(test->response.status, http_result->status)) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = "Unexpected Status";
            break;
        }

        char const* err = body_match(test, http_result);
        if (err) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        err = header_match(test, http_result);
        if (err) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        test_result->status.store(STATUS_OK);
        test_result->verdict = "Success";
    } break;
    case httplib::Error::Canceled:
        test_result->status.store(STATUS_CANCELLED);
        break;
    default:
        test_result->status.store(STATUS_ERROR);
        test_result->verdict = to_string(http_result.error());
        break;
    }

    test_result->http_result = std::forward<httplib::Result>(http_result);
}

void run_test(AppState* app, const Test* test) noexcept {
    httplib::Result result = make_request(app, test);
    if (!app->test_results.contains(test->id)) {
        return;
    }

    TestResult* test_result = &app->test_results.at(test->id);
    test_result->running.store(false);
    test_analysis(app, test, test_result, std::move(result));
}

void run_tests(AppState* app, const std::vector<Test>* tests) noexcept {
    app->thr_pool.purge();
    app->test_results.clear();
    app->runner_params->dockingParams.dockableWindowOfName("Results")->focusWindowAtNextFrame =
        true;

    for (Test test : *tests) {
        app->test_results.try_emplace(test.id, test);

        // add cli settings from parent to a copy
        test.cli_settings = app->get_cli_settings(test.id);

        app->thr_pool.detach_task([app, test = std::move(test)]() { return run_test(app, &test); });
    }
}

void testing_results(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);

    auto deselect_all = [app]() {
        for (auto& [_, rt] : app->test_results) {
            rt.selected = false;
        }
    };

    // TODO: context menu
    if (ImGui::BeginTable("results", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Test");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Verdict");
        ImGui::TableHeadersRow();

        for (auto& [id, result] : app->test_results) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(result.original_test.id));

            // test type and name
            if (ImGui::TableNextColumn()) {
                http_type_button(result.original_test.type);
                ImGui::SameLine();

                if (ImGui::Selectable(result.original_test.endpoint.c_str(), result.selected,
                                      SELECTABLE_FLAGS, ImVec2(0, 0))) {
                    if (ImGui::GetIO().KeyCtrl) {
                        result.selected = !result.selected;
                    } else {
                        deselect_all();
                        result.selected = true;
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (!result.selected) {
                        deselect_all();
                        result.selected = true;
                    }

                    if (ImGui::MenuItem("Details")) {
                        result.open = true;
                    }

                    if (ImGui::MenuItem("Stop")) {
                        app->stop_tests();
                    }

                    ImGui::EndPopup();
                }
            }

            // status
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", TestResultStatusLabels[result.status.load()]);
            }

            // verdict
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", result.verdict.c_str());
            }

            ImGui::PopID();

            if (result.open) {
                auto modal = open_result_details(app, &result);
                result.open &= modal == MODAL_NONE;
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
    auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.2f);
    auto tests_split =
        HelloImGui::DockingSplit("MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2f);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState* app) noexcept {
    auto tab_editor_window =
        HelloImGui::DockableWindow("Editor", "MainDockSpace", [app]() { tabbed_editor(app); });

    auto tests_window =
        HelloImGui::DockableWindow("Tests", "SideBarDockSpace", [app]() { tree_view(app); });

    auto results_window =
        HelloImGui::DockableWindow("Results", "MainDockSpace", [app]() { testing_results(app); });

    auto logs_window =
        HelloImGui::DockableWindow("Logs", "LogDockSpace", []() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, results_window, logs_window};
}

HelloImGui::DockingParams layout(AppState* app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
}

void save_as_file_dialog(AppState* app) noexcept {
    app->save_file_dialog = pfd::save_file("Save To", ".", {"All Files", "*"}, pfd::opt::none);
}

void save_file_dialog(AppState* app) noexcept {
    if (!app->filename.has_value()) {
        save_as_file_dialog(app);
    } else {
        app->save_file();
    }
}

void open_file_dialog(AppState* app) noexcept {
    app->open_file_dialog = pfd::open_file("Open File", ".", {"All Files", "*"}, pfd::opt::none);
}

void show_menus(AppState* app) noexcept {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save As", "Ctrl + Shift + S")) {
            save_as_file_dialog(app);
        } else if (ImGui::MenuItem("Save", "Ctrl + S")) {
            save_file_dialog(app);
        } else if (ImGui::MenuItem("Open", "Ctrl + O")) {
            open_file_dialog(app);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl + Z", nullptr, app->undo_history.can_undo())) {
            app->undo();
        } else if (ImGui::MenuItem("Redo", "Ctrl + Shift + Z", nullptr,
                                   app->undo_history.can_redo())) {
            app->redo();
        }
        ImGui::EndMenu();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (!app->is_running_tests() && arrow("start", ImGuiDir_Right)) {
        // find tests to execute
        std::vector<Test> tests_to_run;
        for (const auto& [id, nested_test] : app->tests) {
            switch (nested_test.index()) {
            case TEST_VARIANT: {
                assert(std::holds_alternative<Test>(nested_test));
                const auto& test = std::get<Test>(nested_test);

                if (!(test.flags & TEST_DISABLED) && !app->parent_disabled(&nested_test)) {
                    tests_to_run.push_back(test);
                }
            } break;
            case GROUP_VARIANT:
                // ignore groups
                break;
            }
        }

        Log(LogLevel::Info, "Started testing for %d tests", tests_to_run.size());
        run_tests(app, &tests_to_run);
    }
    ImGui::PopStyleColor(1);

    if (app->is_running_tests() && ImGui::Button("Stop")) {
        app->stop_tests();
        Log(LogLevel::Warning, "Stopped testing");
    }
}

void show_gui(AppState* app) noexcept {
    auto io = ImGui::GetIO();
#ifndef NDEBUG
    ImGui::ShowDemoWindow();
    ImGuiTestEngine* engine = HelloImGui::GetImGuiTestEngine();
    ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
#endif
    ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);

    if (!app->tree_view_focused) {
        app->selected_tests.clear();
    }

    // saving
    if (app->open_file_dialog.has_value() && app->open_file_dialog->ready()) {
        auto result = app->open_file_dialog->result();

        if (result.size() > 0) {
            app->filename = result[0];
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->open_file();
        }

        app->open_file_dialog = std::nullopt;
    }

    if (app->save_file_dialog.has_value() && app->save_file_dialog->ready()) {
        if (app->save_file_dialog->result().size() > 0) {
            app->filename = app->save_file_dialog->result();
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->save_file();
        }

        app->save_file_dialog = std::nullopt;
    }

    // SHORTCUTS
    //
    // saving
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_as_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        open_file_dialog(app);
    }

    // undo
    if (app->undo_history.can_undo() && io.KeyCtrl && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->undo();
    } else if (app->undo_history.can_redo() && io.KeyCtrl && io.KeyShift &&
               ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->redo();
    }

    // tree view
    if (app->selected_tests.size() > 0) {
        // copy pasting
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            app->copy();
        } else if (!app->selected_tests.contains(0) && io.KeyCtrl &&
                   ImGui::IsKeyPressed(ImGuiKey_X)) {
            app->cut();
        } else if (app->can_paste() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            auto top_layer = app->select_top_layer();
            if (top_layer.size() == 1) {
                NestedTest* parent = &app->tests[top_layer[0]];

                if (std::holds_alternative<Group>(*parent)) {
                    app->paste(&std::get<Group>(*parent));
                    app->undo_history.push_undo_history(app);
                }
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            auto top_layer = app->select_top_layer();
            if (top_layer.size() == 1) {
                app->editor_open_tab(top_layer[0]);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            if (!app->selected_tests.contains(0)) { // root not selected
                app->delete_selected();
            }
        }
    }
}

// program leaks those fonts
// can't do much ig and not a big deal
void load_fonts(AppState* app) noexcept {
    // TODO: fix log window icons
    app->regular_font =
        HelloImGui::LoadFont("fonts/DroidSans.ttf", 15, {.useFullGlyphRange = true});
    app->mono_font =
        HelloImGui::LoadFont("fonts/MesloLGS NF Regular.ttf", 15, {.useFullGlyphRange = true});
}

void post_init(AppState* app) noexcept {
    std::string ini = HelloImGui::IniSettingsLocation(*app->runner_params);
    Log(LogLevel::Debug, "Ini: %s", ini.c_str());
    HelloImGui::HelloImGuiIniSettings::LoadHelloImGuiMiscSettings(ini, app->runner_params);
    Log(LogLevel::Debug, "Theme: %s",
        ImGuiTheme::ImGuiTheme_Name(app->runner_params->imGuiWindowParams.tweakedTheme.Theme));

#if !CPPHTTPLIB_OPENSSL_SUPPORT
    Log(LogLevel::Warning, "Compiled without OpenSSL support! HTTPS will not work!");
#endif
#if !CPPHTTPLIB_ZLIB_SUPPORT
    Log(LogLevel::Warning, "Compiled without ZLib support! Zlib compression will not work!");
#endif
#if !CPPHTTPLIB_BROTLI_SUPPORT
    Log(LogLevel::Warning, "Compiled without Brotli support! Brotli compression will not work!");
#endif

    // NOTE: you have to do this in show_gui instead because imgui is stupid
    // ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
}

void register_tests(AppState* app) noexcept {
    ImGuiTestEngine* e = HelloImGui::GetImGuiTestEngine();
    const char* root_selectable = "**/##0";

    auto delete_all = [app](ImGuiTestContext* ctx) {
        std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_ids;
        ctx->KeyDown(ImGuiKey_ModCtrl);
        for (size_t id : top_groups) {
            ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
        }
        ctx->KeyUp(ImGuiKey_ModCtrl);
        ctx->ItemClick(("**/##" + to_string(top_groups[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__basic_context = IM_REGISTER_TEST(e, "tree_view", "basic_context");
    tree_view__basic_context->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Copy");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Paste");

        delete_all(ctx);
    };

    ImGuiTest* tree_view__copy_paste = IM_REGISTER_TEST(e, "tree_view", "copy_paste");
    tree_view__copy_paste->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 5; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        delete_all(ctx);
    };

    ImGuiTest* tree_view__ungroup = IM_REGISTER_TEST(e, "tree_view", "ungroup");
    tree_view__ungroup->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 3; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        while (app->tests.size() > 1) {
            std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_ids;
            ctx->KeyDown(ImGuiKey_ModCtrl);
            for (size_t id : top_groups) {
                ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
            }
            ctx->KeyUp(ImGuiKey_ModCtrl);
            ctx->ItemClick(("**/##" + to_string(top_groups[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick("**/Ungroup");
        }
    };

    ImGuiTest* tree_view__moving = IM_REGISTER_TEST(e, "tree_view", "moving");
    tree_view__moving->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;

        // test should be last
        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[1])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[1])).c_str());

        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };
}

int main() {
    HelloImGui::RunnerParams runner_params;
    auto app = AppState(&runner_params);

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.rememberTheme = true;

    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };
    runner_params.callbacks.LoadAdditionalFonts = [&app]() { load_fonts(&app); };
    runner_params.callbacks.PostInit = [&app]() { post_init(&app); };
    runner_params.callbacks.RegisterTests = [&app]() { register_tests(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;
    runner_params.useImGuiTestEngine = true;

    ImmApp::AddOnsParams addOnsParams;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

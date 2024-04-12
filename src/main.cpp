#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "immapp/immapp.h"

#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#if OPENSSL
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif
#include "../external/cpp-httplib/out/httplib.h"

#include "BS_thread_pool.hpp"

#include "algorithm"
#include "exception"
#include "fstream"
#include "future"
#include "iterator"
#include "sstream"
#include "stdexcept"
#include "type_traits"
#include "unordered_map"
#include "utility"
#include "variant"

#ifndef NDEBUG
// show test id and parent id in tree view
// #define LABEL_SHOW_ID
#endif

#include "textinputcombo.hpp"

#include "json.hpp"

using HelloImGui::Log;
using HelloImGui::LogLevel;

using std::to_string;

template <typename R>
bool is_ready(std::future<R> const& f) noexcept {
    assert(f.valid());
    return f.wait_until(std::chrono::system_clock::time_point::min()) == std::future_status::ready;
}

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

struct IDVisit {
    size_t operator()(const auto& idable) const noexcept {
        return idable.id;
    }
};

struct SetIDVisit {
    const size_t new_id;
    size_t operator()(auto& idable) const noexcept {
        return idable.id = this->new_id;
    }
};

struct ParentIDVisit {
    size_t operator()(const auto& parent_idable) const noexcept {
        return parent_idable.parent_id;
    }
};

struct SetParentIDVisit {
    const size_t new_id;
    size_t operator()(auto& parent_idable) const noexcept {
        return parent_idable.parent_id = this->new_id;
    }
};

struct LabelVisit {
    const std::string operator()(const auto& labelable) const noexcept {
        return labelable.label();
    }
};

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
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV |
    ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

static constexpr ImGuiSelectableFlags SELECTABLE_FLAGS =
    ImGuiSelectableFlags_SpanAllColumns |
    ImGuiSelectableFlags_AllowOverlap |
    ImGuiSelectableFlags_AllowDoubleClick;

static constexpr ImGuiDragDropFlags DRAG_SOURCE_FLAGS =
    ImGuiDragDropFlags_SourceNoDisableHover |
    ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

bool arrow(const char* label, ImGuiDir dir) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_Border, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
    bool result = ImGui::ArrowButton(label, dir);
    ImGui::PopStyleColor(5);
    return result;
}

void remove_arrow_offset() noexcept {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 8);
}

constexpr ImVec4 rgb_to_ImVec4(int r, int g, int b, int a) noexcept {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
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

template <typename Data>
struct PartialDictElement {
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
template <typename Data>
struct PartialDict {
    using DataType = Data;
    using ElementType = PartialDictElement<Data>;
    using SizeType = std::vector<PartialDictElement<Data>>::size_type;
    std::vector<PartialDictElement<Data>> elements;

    // do not save
    PartialDictElement<Data> add_element;

    bool operator==(const PartialDict& other) const noexcept {
        return this->elements == other.elements;
    }
};

// got from here https://stackoverflow.com/a/60567091
template <class Variant, std::size_t I = 0>
Variant variant_from_index(std::size_t index) {
    if constexpr (I >= std::variant_size_v<Variant>) {
        throw std::runtime_error{"Variant index " + to_string(I + index) + " out of bounds"};
    } else {
        return index == 0
                   ? Variant{std::in_place_index<I>}
                   : variant_from_index<Variant, I + 1>(index - 1);
    }
}

struct SaveState {
    size_t original_size = {};
    size_t load_idx = {};
    std::vector<std::byte> original_buffer;

    // helpers
    template <class T = void>
    void save(const std::byte* ptr, size_t size = sizeof(T)) noexcept {
        std::copy(ptr, ptr + size, std::back_inserter(this->original_buffer));
    }

    std::byte* cur_load(std::size_t offset = 0) const noexcept {
        assert(this->load_idx + offset <= original_buffer.size());
        return (std::byte*)original_buffer.data() + this->load_idx + offset;
    }

    template <class T = void>
    void load(std::byte* ptr, size_t size = sizeof(T)) noexcept {
        std::copy(this->cur_load(), this->cur_load(size), ptr);
        this->load_idx += size;
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void save(T trivial) noexcept {
        this->save<T>((std::byte*)(&trivial));
    }

    template <class T>
        requires(std::is_trivially_copyable<T>::value)
    void load(T& trivial) noexcept {
        this->load((std::byte*)&trivial, sizeof(T));
    }

    void save(const std::string& str) noexcept {
        this->save((std::byte*)str.data(), str.length());
        this->save('\0');
    }

    void load(std::string& str) noexcept {
        size_t length = 0;
        while (*this->cur_load(length) != std::byte(0)) {
            length++;
        }
        str.resize(length);
        this->load((std::byte*)str.data(), length);

        this->load_idx++; // skip over null terminator
    }

    template <class K, class V>
    void save(const std::unordered_map<K, V>& map) noexcept {
        size_t size = map.size();
        this->save(size);

        for (const auto& [k, v] : map) {
            this->save(k);
            this->save(v);
        }
    }

    template <class K, class V>
    void load(std::unordered_map<K, V>& map) noexcept {
        size_t size;
        this->load(size);

        map.clear();
        for (size_t i = 0; i < size; i++) {
            K k;
            V v;
            this->load(k);
            this->load(v);
            map[k] = v;
        }
    }

    template <class... T>
    void save(const std::variant<T...>& variant) noexcept {
        assert(variant.index() != std::variant_npos);
        size_t index = variant.index();
        this->save(index);

        std::visit([this](const auto& s) { this->save(s); }, variant);
    }

    template <class... T>
    void load(std::variant<T...>& variant) noexcept {
        size_t index;
        this->load(index);
        assert(index != std::variant_npos);

        variant = variant_from_index<std::variant<T...>>(index);
        std::visit([this](auto& s) { this->load(s); }, variant);
    }

    template <class Element>
    void save(const std::vector<Element>& vec) noexcept {
        size_t size = vec.size();
        this->save(size);

        for (const auto& elem : vec) {
            this->save(elem);
        }
    }

    template <class Element>
    void load(std::vector<Element>& vec) noexcept {
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

    template <class Data>
    void save(const PartialDict<Data>& pd) noexcept {
        this->save(pd.elements);
    }

    template <class Data>
    void save(const PartialDictElement<Data>& element) noexcept {
        this->save(element.enabled);
        this->save(element.key);
        this->save(element.data);
    }

    template <class Data>
    void load(PartialDict<Data>& pd) noexcept {
        this->load(pd.elements);
    }

    template <class Data>
    void load(PartialDictElement<Data>& element) noexcept {
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
    void load(T& any) noexcept {
        any.load(this);
    }

    void finish_save() noexcept {
        this->original_size = this->original_buffer.size();
        // this->original_buffer.shrink_to_fit();
    }

    void reset_load() noexcept {
        this->load_idx = 0;
    }

    void write(std::ostream& os) const noexcept {
        os.write((char*)&this->original_size, sizeof(std::size_t));
        os.write((char*)this->original_buffer.data(), this->original_buffer.size());
        os.flush();
    }

    void read(std::istream& is) noexcept {
        is.read((char*)&this->original_size, sizeof(std::size_t));

        if (this->original_size != 0) {
            this->original_buffer.reserve(this->original_size);
        }
        this->original_buffer.resize(this->original_size);

        is.read((char*)this->original_buffer.data(), this->original_size);
    }
};

struct UndoHistory {
    size_t undo_idx = {};
    std::vector<SaveState> undo_history;

    // should be called after every edit
    template <class T>
    void push_undo_history(const T* obj) noexcept {
        if (this->undo_idx < this->undo_history.size() - 1) {
            // remove redos
            this->undo_history.resize(this->undo_idx);
        }

        this->undo_history.emplace_back();
        SaveState* new_save = &this->undo_history.back();
        new_save->save(*obj);
        new_save->finish_save();

        this->undo_idx++;
    }

    template <class T>
    void reset_undo_history(const T* obj) noexcept {
        // add initial undo
        this->undo_history.clear();
        this->push_undo_history(obj);
        this->undo_idx = 0;
    }

    constexpr bool can_undo() const noexcept {
        return this->undo_idx > 0;
    }

    template <class T>
    void undo(T* obj) noexcept {
        assert(this->can_undo());

        this->undo_idx--;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    constexpr bool can_redo() const noexcept {
        return this->undo_idx < this->undo_history.size() - 1;
    }

    template <class T>
    void redo(T* obj) noexcept {
        assert(this->can_redo());

        this->undo_idx++;
        this->undo_history[this->undo_idx].load(*obj);
        this->undo_history[this->undo_idx].reset_load();
    }

    UndoHistory() {}
    template <class T>
    UndoHistory(const T* obj) noexcept {
        reset_undo_history(obj);
    }

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
    [MPBD_FILES] = (const char*)"Files",
    [MPBD_TEXT] = (const char*)"Text",
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
        save->save(this->type);
        save->save(this->data);
    }

    void load(SaveState* save) noexcept {
        save->load(this->type);
        save->load(this->data);
    }
};
const char* MultiPartBodyElementData::field_labels[field_count] = {
    (const char*)"Type",
    (const char*)"Data",
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
        save->save(data);
    }

    void load(SaveState* save) noexcept {
        save->load(data);
    }
};
const char* CookiesElementData::field_labels[field_count] = {
    (const char*)"Data",
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
        save->save(data);
    }

    void load(SaveState* save) noexcept {
        save->load(data);
    }
};
const char* ParametersElementData::field_labels[field_count] = {
    (const char*)"Data",
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
        save->save(data);
    }

    void load(SaveState* save) noexcept {
        save->load(data);
    }
};
const char* HeadersElementData::field_labels[field_count] = {
    (const char*)"Data",
};
using Headers = PartialDict<HeadersElementData>;
using HeadersElement = Headers::ElementType;

const char* RequestHeadersLabels[] = {
    (const char*)"A-IM",
    (const char*)"Accept",
    (const char*)"Accept-Charset",
    (const char*)"Accept-Datetime",
    (const char*)"Accept-Encoding",
    (const char*)"Accept-Language",
    (const char*)"Access-Control-Request-Method",
    (const char*)"Access-Control-Request-Headers",
    (const char*)"Authorization",
    (const char*)"Cache-Control",
    (const char*)"Connection",
    (const char*)"Content-Encoding",
    (const char*)"Content-Length",
    (const char*)"Content-MD5",
    (const char*)"Content-Type",
    (const char*)"Cookie",
    (const char*)"Date",
    (const char*)"Expect",
    (const char*)"Forwarded",
    (const char*)"From",
    (const char*)"Host",
    (const char*)"HTTP2-Settings",
    (const char*)"If-Match",
    (const char*)"If-Modified-Since",
    (const char*)"If-None-Match",
    (const char*)"If-Range",
    (const char*)"If-Unmodified-Since",
    (const char*)"Max-Forwards",
    (const char*)"Origin",
    (const char*)"Pragma",
    (const char*)"Prefer",
    (const char*)"Proxy-Authorization",
    (const char*)"Range",
    (const char*)"Referer",
    (const char*)"TE",
    (const char*)"Trailer",
    (const char*)"Transfer-Encoding",
    (const char*)"User-Agent",
    (const char*)"Upgrade",
    (const char*)"Via",
    (const char*)"Warning",
};
static const char* ResponseHeadersLabels[] = {
    (const char*)"Accept-CH",
    (const char*)"Access-Control-Allow-Origin",
    (const char*)"Access-Control-Allow-Credentials",
    (const char*)"Access-Control-Expose-Headers",
    (const char*)"Access-Control-Max-Age",
    (const char*)"Access-Control-Allow-Methods",
    (const char*)"Access-Control-Allow-Headers",
    (const char*)"Accept-Patch",
    (const char*)"Accept-Ranges",
    (const char*)"Age",
    (const char*)"Allow",
    (const char*)"Alt-Svc",
    (const char*)"Cache-Control",
    (const char*)"Connection",
    (const char*)"Content-Disposition",
    (const char*)"Content-Encoding",
    (const char*)"Content-Language",
    (const char*)"Content-Length",
    (const char*)"Content-Location",
    (const char*)"Content-MD5",
    (const char*)"Content-Range",
    (const char*)"Content-Type",
    (const char*)"Date",
    (const char*)"Delta-Base",
    (const char*)"ETag",
    (const char*)"Expires",
    (const char*)"IM",
    (const char*)"Last-Modified",
    (const char*)"Link",
    (const char*)"Location",
    (const char*)"P3P",
    (const char*)"Pragma",
    (const char*)"Preference-Applied",
    (const char*)"Proxy-Authenticate",
    (const char*)"Public-Key-Pins",
    (const char*)"Retry-After",
    (const char*)"Server",
    (const char*)"Set-Cookie",
    (const char*)"Strict-Transport-Security",
    (const char*)"Trailer",
    (const char*)"Transfer-Encoding",
    (const char*)"Tk",
    (const char*)"Upgrade",
    (const char*)"Vary",
    (const char*)"Via",
    (const char*)"Warning",
    (const char*)"WWW-Authenticate",
    (const char*)"X-Frame-Options",
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
    REQUEST_RAW,
    REQUEST_MULTIPART,
};
static const char* RequestBodyTypeLabels[] = {
    [REQUEST_JSON] = (const char*)"JSON",
    [REQUEST_RAW] = (const char*)"Raw",
    [REQUEST_MULTIPART] = (const char*)"Multipart",
};

using RequestBody = std::variant<std::string, MultiPartBody>;

struct Request {
    RequestBodyType body_type = REQUEST_JSON;
    RequestBody body = "{}";

    Cookies cookies;
    Parameters parameters;
    Headers headers;

    void save(SaveState* save) const noexcept {
        save->save(this->body_type);
        save->save(this->body);
        save->save(this->cookies);
        save->save(this->parameters);
        save->save(this->headers);
    }

    void load(SaveState* save) noexcept {
        save->load(this->body_type);
        save->load(this->body);
        save->load(this->cookies);
        save->load(this->parameters);
        save->load(this->headers);
    }
};

enum ResponseBodyType : uint8_t {
    RESPONSE_JSON,
    RESPONSE_HTML,
    RESPONSE_RAW,
    RESPONSE_MULTIPART,
};
static const char* ResponseBodyTypeLabels[] = {
    [RESPONSE_JSON] = (const char*)"JSON",
    [RESPONSE_HTML] = (const char*)"HTML",
    [RESPONSE_RAW] = (const char*)"Raw",
    [RESPONSE_MULTIPART] = (const char*)"Multipart",
};

using ResponseBody = std::variant<std::string, MultiPartBody>;

struct Response {
    std::string status; // a string so user can get hints and write their own status code
    ResponseBodyType body_type = RESPONSE_JSON;
    ResponseBody body = "{}";

    Cookies cookies;
    Headers headers;

    void save(SaveState* save) const noexcept {
        save->save(this->status);
        save->save(this->body_type);
        save->save(this->body);
        save->save(this->cookies);
        save->save(this->headers);
    }

    void load(SaveState* save) noexcept {
        save->load(this->status);
        save->load(this->body_type);
        save->load(this->body);
        save->load(this->cookies);
        save->load(this->headers);
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
    [HTTP_GET] = (const char*)"GET",
    [HTTP_POST] = (const char*)"POST",
    [HTTP_PUT] = (const char*)"PUT",
    [HTTP_DELETE] = (const char*)"DELETE",
    [HTTP_PATCH] = (const char*)"PATCH",
};
static ImVec4 HTTPTypeColor[] = {
    [HTTP_GET] = rgb_to_ImVec4(58, 142, 48, 255),
    [HTTP_POST] = rgb_to_ImVec4(160, 173, 64, 255),
    [HTTP_PUT] = rgb_to_ImVec4(181, 94, 65, 255),
    [HTTP_DELETE] = rgb_to_ImVec4(201, 61, 22, 255),
    [HTTP_PATCH] = rgb_to_ImVec4(99, 22, 90, 255),
};

static const char* HTTPStatusLabels[] = {
    (const char*)"100 Continue",
    (const char*)"101 Switching Protocol",
    (const char*)"102 Processing",
    (const char*)"103 Early Hints",
    (const char*)"200 OK",
    (const char*)"201 Created",
    (const char*)"202 Accepted",
    (const char*)"203 Non-Authoritative Information",
    (const char*)"204 No Content",
    (const char*)"205 Reset Content",
    (const char*)"206 Partial Content",
    (const char*)"207 Multi-Status",
    (const char*)"208 Already Reported",
    (const char*)"226 IM Used",
    (const char*)"300 Multiple Choices",
    (const char*)"301 Moved Permanently",
    (const char*)"302 Found",
    (const char*)"303 See Other",
    (const char*)"304 Not Modified",
    (const char*)"305 Use Proxy",
    (const char*)"306 unused",
    (const char*)"307 Temporary Redirect",
    (const char*)"308 Permanent Redirect",
    (const char*)"400 Bad Request",
    (const char*)"401 Unauthorized",
    (const char*)"402 Payment Required",
    (const char*)"403 Forbidden",
    (const char*)"404 Not Found",
    (const char*)"405 Method Not Allowed",
    (const char*)"406 Not Acceptable",
    (const char*)"407 Proxy Authentication Required",
    (const char*)"408 Request Timeout",
    (const char*)"409 Conflict",
    (const char*)"410 Gone",
    (const char*)"411 Length Required",
    (const char*)"412 Precondition Failed",
    (const char*)"413 Payload Too Large",
    (const char*)"414 URI Too Long",
    (const char*)"415 Unsupported Media Type",
    (const char*)"416 Range Not Satisfiable",
    (const char*)"417 Expectation Failed",
    (const char*)"418 I'm a teapot",
    (const char*)"421 Misdirected Request",
    (const char*)"422 Unprocessable Content",
    (const char*)"423 Locked",
    (const char*)"424 Failed Dependency",
    (const char*)"425 Too Early",
    (const char*)"426 Upgrade Required",
    (const char*)"428 Precondition Required",
    (const char*)"429 Too Many Requests",
    (const char*)"431 Request Header Fields Too Large",
    (const char*)"451 Unavailable For Legal Reasons",
    (const char*)"500 Internal Server Error",
    (const char*)"501 Not Implemented",
    (const char*)"502 Bad Gateway",
    (const char*)"503 Service Unavailable",
    (const char*)"504 Gateway Timeout",
    (const char*)"505 HTTP Version Not Supported",
    (const char*)"506 Variant Also Negotiates",
    (const char*)"507 Insufficient Storage",
    (const char*)"508 Loop Detected",
    (const char*)"510 Not Extended",
    (const char*)"511 Network Authentication Required",
};

enum TestFlags : uint8_t {
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

    const std::string label() const noexcept {
#ifndef LABEL_SHOW_ID
        return this->endpoint + "##" + to_string(this->id);
#else
        return this->endpoint + "__" + to_string(this->id) + "__" + to_string(this->parent_id);
#endif
    }

    void save(SaveState* save) const noexcept {
        save->save(this->id);
        save->save(this->parent_id);
        save->save(this->type);
        save->save(this->flags);
        save->save(this->endpoint);
        save->save(this->request);
        save->save(this->response);
    }

    void load(SaveState* save) noexcept {
        save->load(this->id);
        save->load(this->parent_id);
        save->load(this->type);
        save->load(this->flags);
        save->load(this->endpoint);
        save->load(this->request);
        save->load(this->response);
    }
};

struct TestResult {
    bool selected = true;

    bool running = true;

    Test original_test;
    std::optional<httplib::Result> http_result;

    std::string status;
    std::string verdict;
};

enum GroupFlags : uint8_t {
    GROUP_DISABLED = 1 << 0,
    GROUP_OPEN = 1 << 1,
};

struct Group {
    size_t parent_id;
    size_t id;
    uint8_t flags;

    std::string name;
    std::vector<size_t> children_idx;

    const std::string label() const noexcept {
#ifndef LABEL_SHOW_ID
        return this->name + "##" + to_string(this->id);
#else
        return this->name + "__" + to_string(this->id) + "__" + to_string(this->parent_id);
#endif
    }

    void save(SaveState* save) const noexcept {
        save->save(this->id);
        save->save(this->parent_id);
        save->save(this->flags);
        save->save(this->name);
        save->save(this->children_idx);
    }

    void load(SaveState* save) noexcept {
        save->load(this->id);
        save->load(this->parent_id);
        save->load(this->flags);
        save->load(this->name);
        save->load(this->children_idx);
    }
};

constexpr bool request_eq(const Request* a, const Request* b) noexcept {
    return a->body_type == b->body_type && a->body == b->body && a->cookies == b->cookies && a->headers == b->headers && a->parameters == b->parameters;
}

constexpr bool response_eq(const Response* a, const Response* b) noexcept {
    return a->status == b->status && a->body_type == b->body_type && a->body == b->body && a->cookies == b->cookies && a->headers == b->headers;
}

constexpr bool nested_test_eq(const NestedTest* a, const NestedTest* b) noexcept {
    if (a->index() != b->index()) {
        return false;
    }

    switch (a->index()) {
    case TEST_VARIANT: {
        const auto& test_a = std::get<Test>(*a);
        const auto& test_b = std::get<Test>(*b);
        return test_a.endpoint == test_b.endpoint && test_a.type == test_b.type && request_eq(&test_a.request, &test_b.request) && response_eq(&test_a.response, &test_b.response);
    } break;
    case GROUP_VARIANT:
        const auto& group_a = std::get<Group>(*a);
        const auto& group_b = std::get<Group>(*b);
        return group_a.name == group_b.name;
        break;
    }

    // unreachable
    return false;
}

bool test_comp(const std::unordered_map<size_t, NestedTest>& tests, size_t a_id, size_t b_id) {
    const NestedTest& a = tests.at(a_id);
    const NestedTest& b = tests.at(b_id);

    bool group_a = std::holds_alternative<Group>(a);
    bool group_b = std::holds_alternative<Group>(b);

    if (group_a != group_b) {
        return group_a > group_b;
    }

    std::string label_a = std::visit(LabelVisit(), a);
    std::string label_b = std::visit(LabelVisit(), b);

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
                .name = "root",
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

    std::optional<pfd::open_file> open_file_dialog;
    std::optional<pfd::save_file> save_file_dialog;
    std::optional<std::string> filename;

    BS::thread_pool thr_pool;

    // don't save
    ImFont* regular_font;
    ImFont* mono_font;
    HelloImGui::RunnerParams* runner_params;

    void save(SaveState* save) const noexcept {
        // this save is obviously going to be pretty big so reserve instantly for speed
        save->original_buffer.reserve(512);

        save->save(this->id_counter);
        save->save(this->tests);
    }

    void load(SaveState* save) noexcept {
        save->load(this->id_counter);
        save->load(this->tests);
    }

    void editor_open_tab(size_t id) noexcept {
        if (this->opened_editor_tabs.contains(id)) {
            this->opened_editor_tabs[id].just_opened = true;
        } else {
            this->opened_editor_tabs[id] = EditorTab{
                .original_idx = id,
                .name = std::visit(LabelVisit(), this->tests[id]),
            };
        }
    }

    void focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests) noexcept {
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
                this->select_with_children(std::visit(ParentIDVisit(), nt));
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
        return this->selected_tests.contains(std::visit(ParentIDVisit(), this->tests.at(id)));
    }

    template <bool select = true>
    void select_with_children(size_t id) noexcept {
        if constexpr (select) {
            this->selected_tests.insert(id);
        } else if (!this->parent_selected(id)) {
            this->selected_tests.erase(id);
        }

        assert(this->tests.contains(id));
        NestedTest& nt = this->tests[id];

        if (!std::holds_alternative<Group>(nt)) {
            return;
        }

        Group& group = std::get<Group>(nt);

        for (size_t child_id : group.children_idx) {
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
        // TODO: maybe add some cache for every test that clears every frame?
        // if performance becomes a problem
        size_t id = std::visit(ParentIDVisit(), *nt);
        while (id != -1) {
            assert(this->tests.contains(id));
            nt = &this->tests[id];
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
        assert(group->id != 0); // not root
        auto& parent = this->tests[group->parent_id];
        assert(std::holds_alternative<Group>(parent));
        auto& parent_group = std::get<Group>(parent);

        for (auto child_id : group->children_idx) {
            auto& child = this->tests[child_id];
            std::visit(SetParentIDVisit(parent_group.id), child);
            parent_group.children_idx.push_back(child_id);
        }

        group->children_idx.clear();
    }

    void delete_children(const Group* group) noexcept {
        std::vector<size_t> to_delete = group->children_idx; // DO NOT REMOVE
        // delete_test also removes it from parent->children_idx
        // when iterating over it it creates unexpected behaviour

        for (auto child_id : to_delete) {
            this->delete_test(child_id);
        }

        assert(group->children_idx.size() <= 0); // no remaining children
    }

    void delete_test(size_t id) noexcept {
        assert(this->tests.contains(id));
        NestedTest* test = &this->tests[id];

        size_t parent_id = std::visit(ParentIDVisit(), *test);

        if (std::holds_alternative<Group>(*test)) {
            auto& group = std::get<Group>(*test);
            this->delete_children(&group);
        }

        // remove it's id from parents child id list
        assert(this->tests.contains(parent_id));
        assert(std::holds_alternative<Group>(this->tests[parent_id]));
        auto& parent = std::get<Group>(this->tests[parent_id]);

        size_t count = std::erase(parent.children_idx, id);
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
        auto* parent_test = &this->tests[common_parent_id];
        assert(std::holds_alternative<Group>(*parent_test));
        auto& parent_group = std::get<Group>(*parent_test);

        // remove selected from old parent
        for (auto test_id : this->selected_tests) {
            if (this->parent_selected(test_id)) {
                continue;
            }

            for (auto it = parent_group.children_idx.begin(); it != parent_group.children_idx.end(); it++) {
                if (*it == test_id) {
                    parent_group.children_idx.erase(it);
                    break;
                }
            }
        }

        parent_group.flags |= GROUP_OPEN;
        auto id = ++this->id_counter;
        auto new_group = Group{
            .parent_id = parent_group.id,
            .id = id,
            .flags = GROUP_OPEN,
            .name = "New group",
        };

        // add selected to new parent
        for (auto test_id : this->selected_tests) {
            if (this->parent_selected(test_id)) {
                continue;
            }

            new_group.children_idx.push_back(test_id);
            // set new parent id to tests
            std::visit(SetParentIDVisit(id), this->tests[test_id]);
        }

        parent_group.children_idx.push_back(id);
        this->tests[id] = new_group;
    }

    void copy() noexcept {
        std::unordered_map<size_t, NestedTest> to_copy;
        to_copy.reserve(this->selected_tests.size());
        for (auto sel_id : this->selected_tests) {
            auto* cpy = &this->tests[sel_id];
            to_copy[sel_id] = *cpy;
        }

        this->clipboard = {};
        this->clipboard.save(to_copy);
        this->clipboard.finish_save();
    }

    void cut() noexcept {
        this->copy();
        this->delete_selected();
    }

    constexpr bool can_paste() const noexcept {
        return this->clipboard.original_size > 0;
    }

    void paste(Group* group) noexcept {
        std::unordered_map<size_t, NestedTest> to_paste;
        this->clipboard.load(to_paste);
        this->clipboard.reset_load();

        // increments used ids
        // for tests updates id, parent children_idx (if parent present)
        // for groups should also update all children parent_id
        for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
            auto& [id, nt] = *it;
            // Log(LogLevel::Debug, "id: %zu, variant index: %zu", id, nt.index());

            if (!this->tests.contains(id)) {
                // if the id is free don't do anything
                continue;
            }
            size_t new_id = ++this->id_counter;

            // code to account for if ids present in to_paste are higher than id_counter
            // this could be reproduced by copying and then undoing the creation of copied object
            while (to_paste.contains(new_id)) {
                new_id = ++this->id_counter;
            }

            // update parents children_idx to use new id
            size_t parent_id = std::visit(ParentIDVisit(), nt);
            if (to_paste.contains(parent_id)) {
                // Log(LogLevel::Debug, "parent_id: %zu", parent_id);

                auto& parent = to_paste[parent_id];
                assert(std::holds_alternative<Group>(parent));
                Group& parent_group = std::get<Group>(parent);
                auto& children_idx = parent_group.children_idx;

                // must have it as child
                assert(std::find(children_idx.begin(), children_idx.end(), id) < children_idx.end());
                std::erase(children_idx, id);
                children_idx.push_back(new_id);
            }

            // update groups children parent_id
            if (std::holds_alternative<Group>(nt)) {
                auto& group = std::get<Group>(nt);
                for (size_t child_id : group.children_idx) {
                    assert(to_paste.contains(child_id));
                    std::visit(SetParentIDVisit(new_id), to_paste[child_id]);
                }
            }

            auto node = to_paste.extract(it);
            node.key() = new_id;
            std::visit(SetIDVisit(new_id), node.mapped());
            to_paste.insert(std::move(node));

            // update groups children parent_id
            if (std::holds_alternative<Group>(nt)) {
                auto& group = std::get<Group>(nt);
                std::sort(group.children_idx.begin(), group.children_idx.end(), [&to_paste](size_t a, size_t b) { return test_comp(to_paste, a, b); });
            }
            // Log(LogLevel::Debug, "new id: %zu", new_id);
        }

        // insert into passed in group
        for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
            auto& [id, nt] = *it;

            size_t parent_id = std::visit(ParentIDVisit(), nt);
            if (!to_paste.contains(parent_id)) {
                // Log(LogLevel::Debug, "pasting id: %zu", id);

                group->children_idx.push_back(id);
                std::visit(SetParentIDVisit(group->id), nt);
            }
        }

        group->flags |= GROUP_OPEN;
        this->tests.merge(to_paste);
        this->select_with_children(group->id);
    }

    void move(Group* group) noexcept {
        for (size_t id : this->selected_tests) {
            if (this->parent_selected(id)) {
                continue;
            }

            size_t old_parent = std::visit(ParentIDVisit(), this->tests[id]);
            assert(this->tests.contains(old_parent));
            assert(std::holds_alternative<Group>(this->tests[old_parent]));
            std::erase(std::get<Group>(this->tests[old_parent]).children_idx, id);

            std::visit(SetParentIDVisit(group->id), this->tests[id]);

            group->children_idx.push_back(id);
        }

        group->flags |= GROUP_OPEN;
    }

    void sort(Group& group) noexcept {
        std::sort(
            group.children_idx.begin(), group.children_idx.end(),
            [this](size_t a, size_t b) { return test_comp(this->tests, a, b); });
    }

    bool filter(Group& group) noexcept {
        bool result = true;
        for (size_t child_id : group.children_idx) {
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

    bool filter(Test& test) noexcept {
        return !contains(test.endpoint, this->tree_view_filter);
    }

    // returns true when the value should be filtered *OUT*
    bool filter(NestedTest* nt) noexcept {
        bool filter = std::visit([this](auto& nt) { return this->filter(nt); }, *nt);

        if (filter) {
            this->filtered_tests.insert(std::visit(IDVisit(), *nt));
        } else {
            this->filtered_tests.erase(std::visit(IDVisit(), *nt));
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
        save.write(out);
    }

    void open_file() noexcept {
        assert(this->filename.has_value());

        std::ifstream in(this->filename.value());
        if (!in) {
            Log(LogLevel::Error, "Failed to open file \"%s\"", this->filename->c_str());
            return;
        }

        SaveState save{};
        save.read(in);
        Log(LogLevel::Info, "Loading from '%s': %zuB", this->filename->c_str(), save.original_size);
        save.load(*this);

        this->post_open();
    }

    AppState(HelloImGui::RunnerParams* runner_params) noexcept : runner_params(runner_params) {
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
    size_t parent_id = -1;
    size_t top_selected_count;
};

SelectAnalysisResult select_analysis(AppState* app) noexcept {
    SelectAnalysisResult result;
    result.top_selected_count = app->selected_tests.size();

    auto check_parent = [&result](size_t id) {
        if (!result.same_parent || result.selected_root) {
            return;
        }

        if (result.parent_id == -1) {
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
    size_t nested_test_id = std::visit(IDVisit(), *nested_test);

    if (ImGui::BeginPopupContextItem()) {
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
                if (!std::holds_alternative<Group>(nt) || analysis.same_parent && analysis.parent_id == id || app->selected_tests.contains(id)) {
                    continue;
                }

                if (ImGui::MenuItem(std::visit(LabelVisit(), nt).c_str(), nullptr, false, !changed)) {
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
            if (ImGui::MenuItem("Paste", "Ctrl + V", false, app->can_paste() && analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);

                app->paste(&selected_group);
            }

            if (ImGui::MenuItem("Add a new test", nullptr, false, analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;

                auto id = ++app->id_counter;
                app->tests[id] = (Test{
                    .parent_id = selected_group.id,
                    .id = id,
                    .type = HTTP_GET,
                    .endpoint = "https://example.com",
                });
                selected_group.children_idx.push_back(id);
                app->editor_open_tab(id);
            }

            if (ImGui::MenuItem("Add a new group", nullptr, false, analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                auto id = ++app->id_counter;
                app->tests[id] = (Group{
                    .parent_id = selected_group.id,
                    .id = id,
                    .name = "New group",
                });
                selected_group.children_idx.push_back(id);
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

        if (analysis.same_parent && ImGui::MenuItem("Group Selected", nullptr, false, !analysis.selected_root && !changed)) {
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
    const auto id = std::visit(IDVisit(), test);
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

bool display_tree_view_test(AppState* app, NestedTest& test,
                            float indentation = 0) noexcept {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool ctrl = ImGui::GetIO().KeyCtrl;

    size_t id = std::visit(IDVisit(), test);
    bool changed = false;

    if (app->filtered_tests.contains(id)) {
        return changed;
    }

    ImGui::PushID(id);

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

        if (app->test_results.contains(id) && app->test_results.at(id).running) {
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
        const bool double_clicked = tree_selectable(app, test, ("##" + to_string(leaf.id)).c_str()) && io.MouseDoubleClicked[0];
        if (!changed && !app->selected_tests.contains(0) && ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
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

        if (app->test_results.contains(id) && app->test_results.at(id).running) {
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

        if (!changed && !app->selected_tests.contains(0) && ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
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
            for (size_t child_id : group.children_idx) {
                if (!app->tests.contains(child_id)) {
                    // TODO: remove this, bug hopefully is eliminated
                    Log(LogLevel::Error, "Group %zu has no child %zu", group.id, child_id);
                    printf("Group %zu has no child %zu\n", group.id, child_id);
                } else {
                    assert(app->tests.contains(child_id));
                    changed = changed | display_tree_view_test(app, app->tests[child_id], indentation + 22);
                    if (changed) {
                        break;
                    }
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
    ImGui::PushFont(app->regular_font);

    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputText("##tree_view_search", &app->tree_view_filter);

    if (ImGui::BeginTable("tests", 4)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
        ImGui::TableSetupColumn("selectable", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        changed = changed | display_tree_view_test(app, app->tests[0]);
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
            changed = changed | ComboFilter("##name", &elem->key, hints, hint_count, &elem->cfs.value());
        } else {
            ImGui::SetNextItemWidth(-1);
            changed = changed | ImGui::InputText("##name", &elem->key);
        }
    }

    changed = changed | partial_dict_data_row(app, pd, elem);
    return changed;
}

bool partial_dict_data_row(AppState* app, Cookies* pd, CookiesElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Parameters* pd, ParametersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Headers* pd, HeadersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, MultiPartBody* mpb, MultiPartBodyElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) { // type
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##type", MPBDTypeLabels[elem->data.type])) {
            for (size_t i = 0; i < IM_ARRAYSIZE(MPBDTypeLabels); i++) {
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
            std::string text = files.empty() ? "Select Files" : "Selected " + to_string(files.size()) + " Files (Hover to see names)";
            if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) {
                elem->data.open_file = pfd::open_file("Select Files", ".", {"All Files", "*"}, pfd::opt::multiselect);
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
    using ElementType = PartialDict<Data>::ElementType;

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
            ImGui::PushID(i);
            changed = partial_dict_row(app, pd, elem, hints, hint_count);
            deletion |= elem->to_delete;
            ImGui::PopID();
        }

        if (deletion) {
            changed = true;

            for (int i = pd->elements.size() - 1; i >= 0; i--) {
                if (pd->elements[i].to_delete) {
                    pd->elements.erase(pd->elements.begin() + i);
                }
            }
        }

        ImGui::TableNextRow();
        ImGui::TableNextColumn(); // enabled, skip
        if (ImGui::TableNextColumn()) {
            ImGui::Text("Change this to add new elements");
        }

        ImGui::TableNextRow();
        ImGui::PushID(pd->elements.size());
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

bool editor_test_requests(AppState* app, EditorTab tab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Request")) {
        ImGui::PushID("request");

        if (ImGui::BeginTabItem("Request")) {
            ImGui::Text("Select any of the tabs to edit test's request");
            ImGui::Text("TODO: add a summary of request here");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            if (ImGui::Combo(
                    "Body Type", (int*)&test.request.body_type,
                    RequestBodyTypeLabels, IM_ARRAYSIZE(RequestBodyTypeLabels))) {
                changed = true;

                // TODO: convert between current body types
                switch (test.request.body_type) {
                case REQUEST_JSON:
                case REQUEST_RAW:
                    if (!std::holds_alternative<std::string>(test.request.body)) {
                        test.request.body = "{}";
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
            case REQUEST_RAW:
                ImGui::PushFont(app->mono_font);
                changed = changed | ImGui::InputTextMultiline("##body", &std::get<std::string>(test.request.body), ImVec2(0, 300));
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
            ImGui::Text("TODO: add undeletable params for url");
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.request.parameters, "##parameters");
            ImGui::PopFont();
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
            changed = changed | partial_dict(app, &test.request.headers, "##headers", RequestHeadersLabels, IM_ARRAYSIZE(RequestHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

bool editor_test_response(AppState* app, EditorTab tab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Response")) {
        ImGui::PushID("response");

        if (ImGui::BeginTabItem("Response")) {
            static ComboFilterState s{};
            ComboFilter("Status", &test.response.status, HTTPStatusLabels, IM_ARRAYSIZE(HTTPStatusLabels), &s);
            ImGui::Text("Select any of the tabs to edit test's expected response");
            ImGui::Text("TODO: add a summary of expected response here");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            if (ImGui::Combo(
                    "Body Type", (int*)&test.response.body_type,
                    ResponseBodyTypeLabels, IM_ARRAYSIZE(ResponseBodyTypeLabels))) {
                changed = true;

                // TODO: convert between current body types
                switch (test.response.body_type) {
                case RESPONSE_JSON:
                case RESPONSE_HTML:
                case RESPONSE_RAW:
                    if (!std::holds_alternative<std::string>(test.response.body)) {
                        test.response.body = "";
                    }
                    break;

                case RESPONSE_MULTIPART:
                    if (!std::holds_alternative<MultiPartBody>(test.response.body)) {
                        test.response.body = MultiPartBody{};
                    }
                    break;
                }
            }

            switch (test.response.body_type) {
            case RESPONSE_JSON:
            case RESPONSE_HTML:
            case RESPONSE_RAW:
                ImGui::PushFont(app->mono_font);
                changed = changed | ImGui::InputTextMultiline("##body", &std::get<std::string>(test.response.body), ImVec2(0, 300));
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

            case RESPONSE_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.response.body);
                changed = changed | partial_dict(app, &mpb, "##body");
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
            changed = changed | partial_dict(app, &test.response.headers, "##headers", ResponseHeadersLabels, IM_ARRAYSIZE(ResponseHeadersLabels));
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

ModalResult unsaved_changes(AppState* app) noexcept {
    if (!ImGui::IsPopupOpen("Unsaved Changes")) {
        ImGui::OpenPopup("Unsaved Changes");
    }

    ModalResult result = MODAL_NONE;
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
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

            changed = changed | ImGui::Combo(
                                    "Type", (int*)&test.type,
                                    HTTPTypeLabels, IM_ARRAYSIZE(HTTPTypeLabels));

            changed = changed | editor_test_requests(app, tab, test);
            changed = changed | editor_test_response(app, tab, test);
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
            changed = changed | ImGui::IsItemDeactivatedAfterEdit();
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
        size_t closed_id = -1;
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
                tab.name = std::visit(LabelVisit(), *original);
                tab.just_opened = true; // to force refocus after
                app->undo_history.push_undo_history(app);
            case TAB_NONE:
                break;
            }
        }

        if (closed_id != -1) {
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

httplib::Result make_request(AppState* app, const Test* test) noexcept {
    const auto params = test_params(test);
    const auto headers = test_headers(test);
    const httplib::Progress progress = nullptr;
    httplib::Result result;
    Log(LogLevel::Debug, "Sending %s request to %s", HTTPTypeLabels[test->type], test->label().c_str());
    auto [host, dest] = split_endpoint(test->endpoint);
    Log(LogLevel::Debug, "host: %s, dest: %s", host.c_str(), dest.c_str());
    httplib::Client cli(host);
    switch (test->type) {
    case HTTP_GET:
        // TODO: warn user that get requests will ignore body
        // or implement it for non file body elements
        result = cli.Get(dest, params, headers, progress);
        break;
    case HTTP_POST:
        break;
    case HTTP_PUT:
        break;
    case HTTP_PATCH:
        break;
    case HTTP_DELETE:
        break;
    }
    Log(LogLevel::Debug, "Finished %s request for %s", HTTPTypeLabels[test->type], test->label().c_str());
    return result;
}

void run_test(AppState* app, const Test* test) noexcept {
    httplib::Result result = make_request(app, test);
    Log(LogLevel::Debug, "Got response for %s: %s", test->endpoint.c_str(), to_string(result.error()).c_str());
    if (result.error() == httplib::Error::Success) {
        Log(LogLevel::Debug, "%d %s", result->status, result->body.c_str());
    }

    // TODO: run analysis
    assert(app->test_results.contains(test->id));
    app->test_results.at(test->id).status = "Ok";
    app->test_results.at(test->id).verdict = "Successfully passed";
    app->test_results.at(test->id).running = false;
}

void run_tests(AppState* app, const std::vector<Test>* tests) noexcept {
    app->thr_pool.wait();
    app->test_results.clear();

    for (const auto& test : *tests) {
        // makes a copy of test
        app->thr_pool.detach_task([app, test]() { return run_test(app, &test); });

        app->test_results.insert_or_assign(
            test.id,
            TestResult{
                .original_test = test,
                .status = "Running",
                .verdict = "Pending",
            });
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
            ImGui::PushID(result.original_test.id);

            // test type and name
            if (ImGui::TableNextColumn()) {
                http_type_button(result.original_test.type);
                ImGui::SameLine();
                if (ImGui::Selectable(result.original_test.endpoint.c_str(), result.selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
                    if (ImGui::GetIO().KeyCtrl) {
                        result.selected = !result.selected;
                    } else {
                        deselect_all();
                        result.selected = true;
                    }
                }
            }

            // status
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", result.status.c_str());
            }

            // verdict
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", result.verdict.c_str());
            }

            ImGui::PopID();
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
    auto log_split = HelloImGui::DockingSplit(
        "MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.2);
    auto tests_split = HelloImGui::DockingSplit(
        "MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState* app) noexcept {
    auto tab_editor_window = HelloImGui::DockableWindow(
        "Editor", "MainDockSpace", [app]() { tabbed_editor(app); });

    auto tests_window = HelloImGui::DockableWindow(
        "Tests", "SideBarDockSpace", [app]() { tree_view(app); });

    auto results_window = HelloImGui::DockableWindow(
        "Results", "MainDockSpace", [app]() { testing_results(app); });

    auto logs_window = HelloImGui::DockableWindow(
        "Logs", "LogDockSpace", [app]() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, results_window, logs_window};
}

HelloImGui::DockingParams layout(AppState* app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
}

void save_as_file_dialog(AppState* app) noexcept {
    app->save_file_dialog = pfd::save_file(
        "Save To", ".",
        {"All Files", "*"},
        pfd::opt::none);
}

void save_file_dialog(AppState* app) noexcept {
    if (!app->filename.has_value()) {
        save_as_file_dialog(app);
    } else {
        app->save_file();
    }
}

void open_file_dialog(AppState* app) noexcept {
    app->open_file_dialog = pfd::open_file(
        "Open File", ".",
        {"All Files", "*"},
        pfd::opt::none);
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
        } else if (ImGui::MenuItem("Redo", "Ctrl + Shift + Z", nullptr, app->undo_history.can_redo())) {
            app->redo();
        }
        ImGui::EndMenu();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (arrow("start", ImGuiDir_Right)) {
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
}

void show_gui(AppState* app) noexcept {
    auto io = ImGui::GetIO();
#ifndef NDEBUG
    ImGui::ShowDemoWindow();
    ImGuiTestEngine* engine = HelloImGui::GetImGuiTestEngine();
    ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
#endif
    ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);

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

    // shortcuts
    // saving
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_as_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        open_file_dialog(app);
    }

    // undo
    if (app->undo_history.can_undo() && io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->undo();
    } else if (app->undo_history.can_redo() && io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->redo();
    }

    // copy pasting
    if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
        app->copy();
    } else if (!app->selected_tests.contains(0) && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_X)) {
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

// program leaks those fonts
// can't do much ig and not a big deal
void load_fonts(AppState* app) noexcept {
    // TODO: fix log window icons
    app->regular_font = HelloImGui::LoadFont("fonts/DroidSans.ttf", 15, {.useFullGlyphRange = true});
    app->mono_font = HelloImGui::LoadFont("fonts/MesloLGS NF Regular.ttf", 15, {.useFullGlyphRange = true});
}

void post_init(AppState* app) noexcept {
    std::string ini = HelloImGui::IniSettingsLocation(*app->runner_params);
    Log(LogLevel::Debug, "Ini: %s", ini.c_str());
    HelloImGui::HelloImGuiIniSettings::LoadHelloImGuiMiscSettings(ini, app->runner_params);
    Log(LogLevel::Debug, "Theme: %s", ImGuiTheme::ImGuiTheme_Name(app->runner_params->imGuiWindowParams.tweakedTheme.Theme));
    // NOTE: you have to do this in show_gui instead because imgui is stupid
    // ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
}

void register_tests(AppState* app) noexcept {
    ImGuiTestEngine* e = HelloImGui::GetImGuiTestEngine();
    const char* root_selectable = "**/##0";

    auto delete_all = [app](ImGuiTestContext* ctx) {
        std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_idx;
        ctx->KeyDown(ImGuiKey_ModCtrl);
        for (size_t id : top_groups) {
            ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
        }
        ctx->KeyUp(ImGuiKey_ModCtrl);
        ctx->ItemClick(("**/##" + to_string(top_groups[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");
        size_t items = app->tests.size();

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
            std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_idx;
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
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_idx.size(), 3);

        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_idx;
        // test should be last
        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(), ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[1])).c_str(), ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(), ("**/##" + to_string(top_items[1])).c_str());

        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };
}

int main(int argc, char* argv[]) {
    HelloImGui::RunnerParams runner_params;
    auto app = AppState(&runner_params);

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.rememberTheme = true;

    runner_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

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

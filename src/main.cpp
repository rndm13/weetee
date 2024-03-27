#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"

#include "hello_imgui/hello_imgui_theme.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"
#include "hello_imgui/runner_params.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "immapp/immapp.h"

#include "ImGuiColorTextEdit/TextEditor.h"
#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#if OPENSSL
#define CPPHTTPLIB_OPENSSL_SUPPORT
#endif

#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

#include "BS_thread_pool.hpp"

#include "future"
#include "sstream"
#include "unordered_map"
#include "variant"

using HelloImGui::Log;
using HelloImGui::LogLevel;

using json = nlohmann::json;
using std::to_string;

template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV |
    ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

static constexpr ImGuiSelectableFlags SELECTABLE_FLAGS =
    ImGuiSelectableFlags_SpanAllColumns |
    ImGuiSelectableFlags_AllowOverlap |
    ImGuiSelectableFlags_AllowDoubleClick;

bool arrow(const char* label, ImGuiDir dir) {
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_Border, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
    bool result = ImGui::ArrowButton(label, dir);
    ImGui::PopStyleColor(5);
    return result;
}

void remove_arrow_offset() {
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 8);
}

constexpr ImVec4 rgb_to_ImVec4(int r, int g, int b, int a) noexcept {
    return ImVec4(r / 255.0f, g / 255.0f, b / 255.0f, a / 255.0f);
}

template <typename Data>
struct PartialDictElement {
    bool enabled = true;
    std::string key;
    Data data;

    bool selected = false;
    bool to_delete = false;
};
template <typename Data>
struct PartialDict {
    using DataType = Data;
    using ElementType = PartialDictElement<Data>;
    std::vector<PartialDictElement<Data>> elements;
};

enum MultiPartBodyDataType {
    MPBD_FILES,
    MPBD_TEXT,

    // // Additional
    // MPBD_NUMBER,
    // MPBD_EMAIL,
    // MPBD_URL,
};
static const char* MPBDTypeLabels[] = {
    [MPBD_FILES] = (const char*)"Files",
    [MPBD_TEXT] = (const char*)"Text",
};
using MultiPartBodyData = std::variant<std::vector<std::string>, std::string>;
struct MultiPartBodyElementData {
    MultiPartBodyDataType type;
    MultiPartBodyData data;

    std::optional<pfd::open_file> open_file;

    static constexpr size_t field_count = 2;
    static const char* field_labels[field_count];
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
};
const char* HeadersElementData::field_labels[field_count] = {
    (const char*)"Data",
};
using Headers = PartialDict<HeadersElementData>;
using HeadersElement = Headers::ElementType;

struct Test;
struct Group;
enum NestedTestType : int {
    TEST_VARIANT,
    GROUP_VARIANT,
};
using NestedTest = std::variant<Test, Group>;

enum RequestBodyType : int {
    REQUEST_JSON,
    REQUEST_RAW,
    REQUEST_MULTIPART,
};
static const char* RequestBodyTypeLabels[] = {
    [REQUEST_JSON] = (const char*)"JSON",
    [REQUEST_RAW] = (const char*)"Raw",
    [REQUEST_MULTIPART] = (const char*)"Multipart",
};

// NOTE: maybe change std::string to TextEditor?
using RequestBody = std::variant<std::string, MultiPartBody>;

struct Request {
    RequestBodyType body_type = REQUEST_JSON;
    RequestBody body = "{}";

    Cookies cookies;
    Parameters parameters;
    Headers headers;
};

enum ResponseBodyType : int {
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

// NOTE: maybe change std::string to TextEditor?
using ResponseBody = std::variant<std::string, MultiPartBody>;

struct Response {
    int status;
    ResponseBodyType body_type = RESPONSE_JSON;
    ResponseBody body = "{}";

    Cookies cookies;
    Headers headers;
};

enum HTTPType : uint64_t {
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

enum TestFlags : uint64_t {
    TEST_DISABLED = 1 << 0,
};

struct Test {
    size_t parent_id;
    size_t id;
    uint64_t flags;

    HTTPType type;
    std::string endpoint;

    Request request;
    Response response;

    TextEditor editor;

    const std::string label() const noexcept {
        return this->endpoint + "##" + to_string(this->id);
    }
};

struct TestResult {
    httplib::Response response;
};

enum GroupFlags : uint64_t {
    GROUP_DISABLED = 1 << 0,
    GROUP_OPEN = 1 << 1,
};

struct Group {
    size_t parent_id;
    size_t id;
    uint64_t flags;

    std::string name;
    std::vector<size_t> children_idx;

    const std::string label() const noexcept {
        return this->name + "##" + to_string(this->id);
    }
};

bool nested_test_eq(const NestedTest* a, const NestedTest* b) noexcept {
    if (a->index() != b->index()) {
        return false;
    }

    switch (a->index()) {
    case TEST_VARIANT: {
        const auto& test_a = std::get<Test>(*a);
        const auto& test_b = std::get<Test>(*b);
        // TODO: check request and response
        return test_a.endpoint == test_b.endpoint && test_a.type == test_b.type;
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

struct IDVisit {
    size_t operator()(const auto& idable) const noexcept {
        return idable.id;
    }
};

struct ParentIDVisit {
    size_t operator()(const auto& parent_idable) const noexcept {
        return parent_idable.parent_id;
    }
};

struct ParentIDSetVisit {
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

// keys are ids and values are for separate for editing (must be saved to apply changes)
struct EditorTab {
    bool open = true;
    bool just_opened;
    NestedTest* original;
    NestedTest edit;
};

struct AppState {
    BS::thread_pool thr_pool;
    // httplib::Client cli;
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

    std::unordered_map<size_t, EditorTab> opened_editor_tabs = {};
    std::unordered_set<size_t> selected_tests = {};

    ImFont* regular_font;
    ImFont* mono_font;
    HelloImGui::RunnerParams* runner_params;
};

bool nested_test_parent_disabled(AppState* app, const NestedTest* nt) noexcept {
    // TODO: maybe add some cache for every test that clears every frame?
    // if performance becomes a problem
    size_t id = std::visit(ParentIDVisit(), *nt);
    while (id != -1) {
        nt = &app->tests[id];
        assert(std::holds_alternative<Group>(*nt));
        const Group& group = std::get<Group>(*nt);
        if (group.flags & GROUP_DISABLED) {
            return true;
        }
        id = group.parent_id;
    }
    return false;
}

void move_children_up(AppState* app, Group* group) noexcept {
    assert(group->id != 0); // not root
    auto& parent = app->tests[group->parent_id];
    assert(std::holds_alternative<Group>(parent));
    auto& parent_group = std::get<Group>(parent);

    for (auto child_id : group->children_idx) {
        auto& child = app->tests[child_id];
        std::visit(ParentIDSetVisit(parent_group.id), child);
        parent_group.children_idx.push_back(child_id);
    }
    group->children_idx.clear();
}

void delete_group(AppState* app, const Group* group) noexcept;
void delete_test(AppState* app, NestedTest* test) noexcept;

void delete_group(AppState* app, const Group* group) noexcept {
    for (auto child_id : group->children_idx) {
        delete_test(app, &app->tests[child_id]);
    }
}

void delete_test(AppState* app, NestedTest* test) noexcept {
    size_t id = std::visit(IDVisit(), *test);
    size_t parent_id = std::visit(ParentIDVisit(), *test);

    if (std::holds_alternative<Group>(*test)) {
        auto& group = std::get<Group>(*test);
        delete_group(app, &group);
    }

    // remove it's id from parents child id list
    auto& parent = std::get<Group>(app->tests[parent_id]);
    for (auto it = parent.children_idx.begin(); it != parent.children_idx.end(); it++) {
        if (*it == id) {
            parent.children_idx.erase(it);
            break;
        };
    }

    // remove from tests
    app->tests.erase(id);
    app->opened_editor_tabs.erase(id);
}

void editor_open_tab(AppState* app, size_t id) {
    if (app->opened_editor_tabs.contains(id)) {
        app->opened_editor_tabs[id].just_opened = true;
    } else {
        app->opened_editor_tabs[id] = {
            .just_opened = true,
            .original = &app->tests[id],
            .edit = app->tests[id]};
    }
}

bool context_menu_tree_view(AppState* app, NestedTest* nested_test) noexcept {
    bool change = false;
    size_t nested_test_id = std::visit(IDVisit(), *nested_test);

    if (ImGui::BeginPopupContextItem()) {
        if (!app->selected_tests.contains(nested_test_id)) {
            app->selected_tests.clear();
            app->selected_tests.insert(nested_test_id);
        }

        // analyzes selected tests first
        // looks for this data
        bool group = false;
        bool test = false;
        bool same_parent = true;
        bool selected_root = false;
        size_t parent_id = -1;
        size_t selected_count = app->selected_tests.size();
        // TODO: allow same_parent when a directory was selected with all it's children_idx
        // example:
        // root
        // * \ group
        // *   \ test 1
        // * \ test 2
        // and check for selected parent in tests
        auto check_parent = [&parent_id, &same_parent, &selected_root](size_t id) {
            if (!same_parent || selected_root) {
                return;
            }

            if (parent_id == -1) {
                parent_id = id;
            } else if (parent_id != id) {
                same_parent = false;
            }
        };

        for (auto test_idx : app->selected_tests) {
            auto* selected = &app->tests[test_idx];

            switch (selected->index()) {
            case TEST_VARIANT: {
                assert(std::holds_alternative<Test>(*selected));
                auto& selected_test = std::get<Test>(*selected);
                test = true;
                check_parent(selected_test.parent_id);
            } break;
            case GROUP_VARIANT: {
                assert(std::holds_alternative<Group>(*selected));
                auto& selected_group = std::get<Group>(*selected);
                group = true;
                selected_root |= selected_group.id == 0;
                check_parent(selected_group.parent_id);
            } break;
            }
        }

        if (group && !test) {
            assert(std::holds_alternative<Group>(*nested_test));
            auto& selected_group = std::get<Group>(*nested_test);
            if (selected_count == 1) {
                if (ImGui::MenuItem("Add a new test")) {
                    change = true;
                    selected_group.flags |= GROUP_OPEN;

                    auto id = ++app->id_counter;
                    app->tests[id] = (Test{
                        .parent_id = selected_group.id,
                        .id = id,
                        .type = HTTP_GET,
                        .endpoint = "https://example.com",
                    });
                    selected_group.children_idx.push_back(id);
                }

                if (ImGui::MenuItem("Add a new group")) {
                    change = true;
                    selected_group.flags |= GROUP_OPEN;
                    auto id = ++app->id_counter;
                    app->tests[id] = (Group{
                        .parent_id = selected_group.id,
                        .id = id,
                        .name = "New group",
                    });
                    selected_group.children_idx.push_back(id);
                }
            }

            if (ImGui::MenuItem("Ungroup", nullptr, false, !selected_root)) {
                change = true;
                for (auto selected_idx : app->selected_tests) {
                    auto& selected = app->tests[selected_idx];

                    assert(std::holds_alternative<Group>(selected));
                    auto& selected_group = std::get<Group>(selected);

                    move_children_up(app, &selected_group);
                    delete_test(app, &selected);
                }
            }
            ImGui::Separator();
        }

        if (ImGui::MenuItem("Edit", nullptr, false, selected_count == 1)) {
            editor_open_tab(app, nested_test_id);
        }

        if (ImGui::MenuItem("Delete", nullptr, false, !selected_root)) {
            change = true;
            for (auto test_idx : app->selected_tests) {
                delete_test(app, &app->tests[test_idx]);
            }
        }

        if (same_parent && ImGui::MenuItem("Group Selected", nullptr, false, !selected_root)) {
            change = true;

            auto* parent_test = &app->tests[parent_id];
            assert(std::holds_alternative<Group>(*parent_test));
            auto& parent_group = std::get<Group>(*parent_test);

            // remove selected from old parent
            for (auto test_idx : app->selected_tests) {
                for (auto it = parent_group.children_idx.begin(); it != parent_group.children_idx.end(); it++) {
                    if (*it == test_idx) {
                        parent_group.children_idx.erase(it);
                        break;
                    }
                }
            }

            parent_group.flags |= GROUP_OPEN;
            auto id = ++app->id_counter;
            auto new_group = Group{
                .parent_id = parent_group.id,
                .id = id,
                .flags = GROUP_OPEN,
                .name = "New group",
            };
            // add selected to new parent
            for (auto test_idx : app->selected_tests) {
                new_group.children_idx.push_back(test_idx);
                // set new parent id to tests
                std::visit(ParentIDSetVisit(id), app->tests[test_idx]);
            }

            parent_group.children_idx.push_back(id);
            app->tests[id] = new_group;
        }

        ImGui::EndPopup();
    }

    if (change) {
        app->selected_tests.clear();
    }

    return change;
}

bool tree_selectable(AppState* app, NestedTest& test, const char* label) noexcept {
    const auto id = std::visit(IDVisit(), test);
    bool item_is_selected = app->selected_tests.contains(id);
    if (ImGui::Selectable(label, item_is_selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
        if (ImGui::GetIO().KeyCtrl) {
            if (item_is_selected) {
                app->selected_tests.erase(id);
            } else {
                app->selected_tests.insert(id);
            }
        } else {
            app->selected_tests.clear();
            app->selected_tests.insert(id);
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

void display_tree_test(AppState* app, NestedTest& test,
                       float indentation = 0) noexcept {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool ctrl = ImGui::GetIO().KeyCtrl;

    ImGui::PushID(std::visit(IDVisit(), test));

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
        ImSpinner::SpinnerIncDots("running", 5, 1);

        ImGui::TableNextColumn(); // enabled / disabled

        bool parent_disabled = nested_test_parent_disabled(app, &test);
        if (parent_disabled) {
            ImGui::BeginDisabled();
        }

        bool enabled = !(leaf.flags & TEST_DISABLED);
        if (ImGui::Checkbox("##enabled", &enabled)) {
            if (!enabled) {
                leaf.flags |= TEST_DISABLED;
            } else {
                leaf.flags &= ~TEST_DISABLED;
            }
        }

        if (parent_disabled) {
            ImGui::EndDisabled();
        }

        ImGui::TableNextColumn(); // selectable
        const bool double_clicked = tree_selectable(app, test, ("##" + to_string(leaf.id)).c_str()) && io.MouseDoubleClicked[0];
        const bool changed = context_menu_tree_view(app, &test);

        if (!changed && double_clicked) {
            editor_open_tab(app, leaf.id);
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
        ImSpinner::SpinnerIncDots("running", 5, 1);

        ImGui::TableNextColumn(); // enabled / disabled

        bool parent_disabled = nested_test_parent_disabled(app, &test);
        if (parent_disabled) {
            ImGui::BeginDisabled();
        }

        bool enabled = !(group.flags & GROUP_DISABLED);
        if (ImGui::Checkbox("##enabled", &enabled)) {
            if (!enabled) {
                group.flags |= GROUP_DISABLED;
            } else {
                group.flags &= ~GROUP_DISABLED;
            }
        }

        if (parent_disabled) {
            ImGui::EndDisabled();
        }

        ImGui::TableNextColumn(); // selectable
        const bool clicked = tree_selectable(app, test, ("##" + to_string(group.id)).c_str());
        if (clicked) {
            group.flags ^= GROUP_OPEN; // toggle
        }
        const bool changed = context_menu_tree_view(app, &test);

        if (group.flags & GROUP_OPEN) {
            if (!changed) {
                for (size_t child_id : group.children_idx) {
                    display_tree_test(app, app->tests[child_id],
                                      indentation + 22);
                }
            }
        }
    } break;
    }

    ImGui::PopID();
}

void test_tree_view(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);
    if (ImGui::BeginTable("tests", 4)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
        ImGui::TableSetupColumn("selectable", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        display_tree_test(app, app->tests[0]);
        ImGui::EndTable();
    }
    ImGui::PopFont();
}

template <typename Data>
bool partial_dict_row(AppState* app, PartialDict<Data>* pd, PartialDictElement<Data>* elem) noexcept {
    bool changed = false;
    auto select_only_this = [pd, elem]() {
        for (auto& e : pd->elements) {
            e.selected = false;
        }
        elem->selected = true;
    };
    if (ImGui::TableNextColumn()) { // enabled and selectable
        // TODO: make this look less stupid
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
        ImGui::SetNextItemWidth(-1);
        changed = changed | ImGui::InputText("##name", &elem->key);
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
        if (ImGui::Combo("##type", (int*)&elem->data.type, MPBDTypeLabels, IM_ARRAYSIZE(MPBDTypeLabels))) {
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
                elem->data.open_file = std::nullopt;
            }
            break;
        }
    }
    return changed;
}

template <typename Data>
void partial_dict(AppState* app, PartialDict<Data>* pd, const char* label) noexcept {
    using DataType = PartialDict<Data>::DataType;
    using ElementType = PartialDict<Data>::ElementType;
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
            partial_dict_row(app, pd, elem);
            deletion |= elem->to_delete;
            ImGui::PopID();
        }
        if (deletion) {
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
        static ElementType elem = {};
        if (partial_dict_row(app, pd, &elem)) {
            pd->elements.push_back(elem);
            elem = {};
        }
        ImGui::PopID();
        ImGui::EndTable();
    }
}

void editor_test_requests(AppState* app, EditorTab tab, Test& test) noexcept {
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

                // TODO: convert between current body types
                switch (test.request.body_type) {
                case REQUEST_JSON:
                    test.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Json());
                    if (!std::holds_alternative<std::string>(test.request.body)) {
                        test.request.body = "{}";
                        test.editor.SetText("{}");
                        // TODO: allow for palette change within view settings
                        test.editor.SetPalette(TextEditor::GetDarkPalette());
                    }
                    break;

                case REQUEST_RAW:
                    test.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Json());
                    if (!std::holds_alternative<std::string>(test.request.body)) {
                        test.request.body = "";
                        test.editor.SetText("");
                        // TODO: allow for palette change within view settings
                        test.editor.SetPalette(TextEditor::GetDarkPalette());
                    }
                    break;

                case REQUEST_MULTIPART:
                    test.request.body = MultiPartBody{};
                    break;
                }
            }

            switch (test.request.body_type) {
            case REQUEST_JSON:
                ImGui::SameLine();
                if (ImGui::Button("Format")) {
                    try {
                        test.editor.SetText(json::parse(test.editor.GetText()).dump(4));
                    } catch (json::parse_error& error) {
                        Log(LogLevel::Error, "Failed to parse json for formatting: %s", error.what());
                    }
                }

                ImGui::PushFont(app->mono_font);
                test.editor.Render("##body", false, ImVec2(0, 300));
                ImGui::PopFont();

                test.request.body = test.editor.GetText();
                break;

            case REQUEST_RAW:
                ImGui::PushFont(app->mono_font);
                test.editor.Render("##body", false, ImVec2(0, 300));
                ImGui::PopFont();
                test.request.body = test.editor.GetText();
                break;

            case REQUEST_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.request.body);
                partial_dict(app, &mpb, "##body");
                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Parameters")) {
            ImGui::Text("TODO: add undeletable params for url");
            ImGui::PushFont(app->mono_font);
            partial_dict(app, &test.request.parameters, "##parameters");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cookies")) {
            ImGui::PushFont(app->mono_font);
            partial_dict(app, &test.request.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::Text("TODO: make a suggestions popup (different for request/response)");
            ImGui::PushFont(app->mono_font);
            partial_dict(app, &test.request.headers, "##headers");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }
}

void editor_test_response(AppState* app, EditorTab tab, Test& test) noexcept {
    if (ImGui::BeginTabBar("Response")) {
        ImGui::PushID("response");

        if (ImGui::BeginTabItem("Response")) {
            ImGui::Text("Select any of the tabs to edit test's expected response");
            ImGui::InputInt("Status", &test.response.status);
            ImGui::Text("TODO: add a summary of expected response here");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            if (ImGui::Combo(
                    "Body Type", (int*)&test.response.body_type,
                    ResponseBodyTypeLabels, IM_ARRAYSIZE(ResponseBodyTypeLabels))) {

                // TODO: convert between current body types
                switch (test.response.body_type) {
                case RESPONSE_JSON:
                case RESPONSE_HTML:
                case RESPONSE_RAW:
                    test.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Json());
                    if (!std::holds_alternative<std::string>(test.response.body)) {
                        test.response.body = "";
                        test.editor.SetText("");
                        // TODO: allow for palette change within view settings
                        test.editor.SetPalette(TextEditor::GetDarkPalette());
                    }
                    break;

                case RESPONSE_MULTIPART:
                    test.response.body = MultiPartBody{};
                    break;
                }
            }

            switch (test.response.body_type) {
            case RESPONSE_JSON:
                ImGui::SameLine();
                if (ImGui::Button("Format")) {
                    try {
                        test.editor.SetText(json::parse(test.editor.GetText()).dump(4));
                    } catch (json::parse_error& error) {
                        Log(LogLevel::Error, (std::string("Failed to parse json for formatting: ") + error.what()).c_str());
                    }
                }
            case RESPONSE_HTML:
            case RESPONSE_RAW:
                ImGui::PushFont(app->mono_font);
                test.editor.Render("##body", false, ImVec2(0, 300));
                ImGui::PopFont();
                test.response.body = test.editor.GetText();
                break;

            case RESPONSE_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.response.body);
                partial_dict(app, &mpb, "##body");
                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Set Cookies")) {
            ImGui::PushFont(app->mono_font);
            partial_dict(app, &test.response.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::Text("TODO: make a suggestions popup (different for request/response)");
            ImGui::PushFont(app->mono_font);
            partial_dict(app, &test.response.headers, "##headers");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }
}

enum ModalResult {
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

enum EditorTabResult {
    TAB_NONE,
    TAB_CLOSED,
    TAB_SAVED,
    TAB_SAVE_CLOSED,
};

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab) noexcept {
    assert(std::holds_alternative<Test>(tab.edit));
    auto& test = std::get<Test>(tab.edit);

    auto changed = [&tab]() {
        return !nested_test_eq(&tab.edit, tab.original);
    };

    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(
            std::visit(LabelVisit(), *tab.original).c_str(), &tab.open,
            (changed() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None) |
                (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {
        ImGui::InputText("Endpoint", &test.endpoint);
        ImGui::Combo(
            "Type", (int*)&test.type,
            HTTPTypeLabels, IM_ARRAYSIZE(HTTPTypeLabels));

        editor_test_requests(app, tab, test);
        editor_test_response(app, tab, test);

        if (ImGui::Button("Save")) {
            result = TAB_SAVED;
        }

        ImGui::EndTabItem();
    }
    if (!tab.open && changed()) {
        switch (unsaved_changes(app)) {
        case MODAL_CONTINUE:
            result = TAB_CLOSED;
            break;
        case MODAL_SAVE:
            result = TAB_SAVE_CLOSED;
            break;
        case MODAL_CANCEL:
            tab.open = true;
            break;
        default:
            break;
        }
    }
    return result;
}

EditorTabResult editor_tab_group(AppState* app, EditorTab& tab) noexcept {
    assert(std::holds_alternative<Group>(tab.edit));
    auto& group = std::get<Group>(tab.edit);

    auto changed = [&tab]() {
        return !nested_test_eq(&tab.edit, tab.original);
    };

    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(
            std::visit(LabelVisit(), *tab.original).c_str(), &tab.open,
            (changed() ? ImGuiTabItemFlags_UnsavedDocument : ImGuiTabItemFlags_None) |
                (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {
        ImGui::InputText("Name", &group.name);

        if (ImGui::Button("Save")) {
            result = TAB_SAVED;
        }

        ImGui::EndTabItem();
    }
    if (!tab.open && changed()) {
        switch (unsaved_changes(app)) {
        case MODAL_CONTINUE:
            result = TAB_CLOSED;
            break;
        case MODAL_SAVE:
            result = TAB_SAVE_CLOSED;
            break;
        case MODAL_CANCEL:
            tab.open = true;
            break;
        default:
            break;
        }
    }
    return result;
}

void tabbed_editor(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);
    if (ImGui::BeginTabBar("editor")) {
        size_t closed_id = -1;
        for (auto& [id, tab] : app->opened_editor_tabs) {
            const NestedTest* original = tab.original;
            EditorTabResult result;
            switch (tab.edit.index()) {
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
            case TAB_SAVE_CLOSED:
                closed_id = id;
            case TAB_SAVED:
                *tab.original = tab.edit;
                tab.just_opened = true;
                break;
            case TAB_CLOSED:
                closed_id = id;
                break;
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
        "Tests", "SideBarDockSpace", [app]() { test_tree_view(app); });

    auto logs_window = HelloImGui::DockableWindow(
        "Logs", "LogDockSpace", [app]() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, logs_window};
}

HelloImGui::DockingParams layout(AppState* app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
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

httplib::Headers test_headers(Test* test) noexcept {
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

httplib::Params test_params(Test* test) noexcept {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!param.enabled) {
            continue;
        }
        result.emplace(param.key, param.data.data);
    };

    return result;
}

httplib::Result make_request(AppState* app, Test* test) noexcept {
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

TestResult run_test(AppState* app, Test test) noexcept {
    // copy test to not crash if test somehow gets deleted while executing
    // maybe forbid test deletion while executing?
    const auto result = make_request(app, &test);
    Log(LogLevel::Debug, "Got response for %s: %s", test.endpoint.c_str(), to_string(result.error()).c_str());
    if (result.error() == httplib::Error::Success) {
        Log(LogLevel::Debug, "%d %s", result->status, result->body.c_str());
    }

    return {}; // TODO: return proper value
}

void run_tests(AppState* app, std::vector<Test>* tests) noexcept {
    for (const auto& test : *tests) {
        auto result = app->thr_pool.submit_task([app, &test]() { run_test(app, test); });
        result.wait();
    }
}

void show_menus(AppState* app) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (arrow("start", ImGuiDir_Right)) {

        // find tests to execute
        std::vector<Test> tests_to_run;
        for (const auto& [id, nested_test] : app->tests) {
            switch (nested_test.index()) {
            case TEST_VARIANT: {
                assert(std::holds_alternative<Test>(nested_test));
                const auto& test = std::get<Test>(nested_test);

                if (!(test.flags & TEST_DISABLED) && !nested_test_parent_disabled(app, &nested_test)) {
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
    ImGui::ShowDemoWindow();
    ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
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

int main(int argc, char* argv[]) {
    HelloImGui::RunnerParams runner_params;
    auto app = AppState{
        .thr_pool = BS::thread_pool(),
        .runner_params = &runner_params,
    };

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.rememberTheme = true;

    runner_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };
    runner_params.callbacks.LoadAdditionalFonts = [&app]() { load_fonts(&app); };
    runner_params.callbacks.PostInit = [&app]() { post_init(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;

    ImmApp::AddOnsParams addOnsParams;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

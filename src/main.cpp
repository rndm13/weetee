#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "immapp/immapp.h"

#include "ImGuiColorTextEdit/TextEditor.h"
#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"
#include <sstream>

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

#include "unordered_map"
#include "variant"

using HelloImGui::Log;
using HelloImGui::LogLevel;

using json = nlohmann::json;

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

enum HTTPType : int {
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

// NOTE: maybe change std::string to TextEditor?
using RequestBody = std::variant<std::string, MultiPartBody>;

struct Test;
struct Group;
enum NestedTestType : int {
    TEST_TYPE,
    GROUP_TYPE,
};
using NestedTest = std::variant<Test, Group>;

struct Test {
    size_t parent_id;
    uint64_t id;

    HTTPType type;
    std::string endpoint;

    bool enabled = true;

    RequestBodyType body_type = REQUEST_JSON;
    RequestBody body = "{}";
    
    Cookies cookies;
    Parameters parameters;
    std::string headers;

    TextEditor editor;

    const std::string label() const noexcept {
        return this->endpoint + "##" + std::to_string(this->id);
    }
};

struct Group {
    size_t parent_id;
    uint64_t id;

    std::string name;
    std::vector<size_t> children_idx;

    bool open = false;

    bool enabled = true;

    const std::string label() const noexcept {
        return this->name + "##" + std::to_string(this->id);
    }
};

struct IDVisit {
    uint64_t operator()(const auto& idable) const noexcept {
        return idable.id;
    }
};

struct LabelVisit {
    const std::string operator()(const auto& labelable) const noexcept {
        return labelable.label();
    }
};

// keys are ids and values are for separate for editing (must be saved to apply changes)
struct EditorTab {
    bool just_opened;
    NestedTest* original;
    NestedTest edit;
};

struct AppState {
    httplib::Client cli;
    uint64_t id_counter = 0;
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
};

void delete_group(AppState* app, const Group* group) noexcept;
void delete_test(AppState* app, NestedTest test) noexcept;

void delete_group(AppState* app, const Group* group) noexcept {
    for (auto child_id : group->children_idx) {
        auto child = app->tests[child_id];
        delete_test(app, child);
    }
}

void delete_test(AppState* app, NestedTest test) noexcept {
    size_t id;
    size_t parent_id;

    switch (test.index()) {
    case TEST_TYPE: {
        auto& leaf = std::get<Test>(test);

        id = leaf.id;
        parent_id = leaf.parent_id;
    } break;
    case GROUP_TYPE: {
        auto& group = std::get<Group>(test);

        delete_group(app, &group);
        id = group.id;
        parent_id = group.parent_id;
    } break;
    }

    // remove it's id from parents child id list
    auto& parent = std::get<Group>(app->tests.at(parent_id));
    for (auto it = parent.children_idx.begin(); it != parent.children_idx.end();
         it++) {
        if (*it == id) {
            parent.children_idx.erase(it);
            break;
        };
    }

    // remove from tests
    app->tests.erase(id);
    app->opened_editor_tabs.erase(id);
}

// TODO: make this a single function instead that analyzes selected tests instead
bool context_menu_visitor(AppState* app, Group* group) noexcept {
    bool change = false;
    if (ImGui::BeginPopupContextItem()) {
        if (!app->selected_tests.contains(group->id)) {
            app->selected_tests.clear();
            app->selected_tests.insert(group->id);
        }
        if (ImGui::MenuItem("Edit")) {
            app->opened_editor_tabs[group->id] = {
                .just_opened = true,
                .original = &app->tests[group->id],
                .edit = *group};
        }

        if (ImGui::MenuItem("Add a new test")) {
            change = true;
            group->open = true;

            auto id = ++app->id_counter;
            app->tests[id] = (Test{
                .parent_id = group->id,
                .id = id,
                .type = HTTP_GET,
                .endpoint = "https://example.com",
            });
            group->children_idx.push_back(id);
        }

        if (ImGui::MenuItem("Add a new group")) {
            change = true;
            group->open = true;
            auto id = ++app->id_counter;
            app->tests[id] = (Group{
                .parent_id = group->id,
                .id = id,
                .name = "New group",
            });
            group->children_idx.push_back(id);
        }

        if (ImGui::MenuItem("Delete", nullptr, false, group->id != 0)) {
            delete_test(app, *group);
            change = true;
        }
        ImGui::EndPopup();
    }
    return change;
}

bool context_menu_visitor(AppState* app, Test* test) noexcept {
    bool change = false;
    if (ImGui::BeginPopupContextItem()) {
        if (!app->selected_tests.contains(test->id)) {
            app->selected_tests.clear();
            app->selected_tests.insert(test->id);
        }

        if (ImGui::MenuItem("Edit")) {
            app->opened_editor_tabs[test->id] = {
                .just_opened = true,
                .original = &app->tests[test->id],
                .edit = *test};
        }

        if (ImGui::MenuItem("Delete", nullptr, false, test->id != 0)) {
            delete_test(app, *test);
            change = true;
        }
        ImGui::EndPopup();
    }
    return change;
}

bool tree_selectable(AppState* app, NestedTest& test, const char* label) noexcept {
    const auto id = std::visit(IDVisit(), test);
    bool item_is_selected = app->selected_tests.contains(id);
    if (ImGui::Selectable(label, item_is_selected, SELECTABLE_FLAGS,
                          ImVec2(0, 21))) {
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

void display_tree_test(AppState* app, NestedTest& test,
                       float indentation = 0) noexcept {
    ImGuiWindow* window = ImGui::GetCurrentWindow();
    const bool ctrl = ImGui::GetIO().KeyCtrl;

    ImGui::PushID(std::visit(IDVisit(), test));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);
    switch (test.index()) {
    case TEST_TYPE: {
        auto& leaf = std::get<Test>(test);
        const auto io = ImGui::GetIO();
        ImGui::TableNextColumn(); // test
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
        const bool double_clicked = tree_selectable(app, test, leaf.label().c_str()) && io.MouseDoubleClicked[0];
        const bool changed = context_menu_visitor(app, &leaf);
        ImGui::TableNextColumn(); // spinner for running tests
        ImSpinner::SpinnerIncDots("running", 5, 1);
        ImGui::TableNextColumn(); // enabled / disabled
        // TODO: make this look better
        ImGui::Checkbox("##enabled", &leaf.enabled);

        if (!changed && double_clicked) {
            app->opened_editor_tabs[leaf.id] = {
                .just_opened = true,
                .original = &app->tests[leaf.id],
                .edit = leaf};
        }
    } break;
    case GROUP_TYPE: {
        auto& group = std::get<Group>(test);

        ImGui::TableNextColumn(); // test
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
        if (group.open) {
            arrow("down", ImGuiDir_Down);
        } else {
            arrow("right", ImGuiDir_Right);
        }
        ImGui::SameLine();
        remove_arrow_offset();
        const bool clicked = tree_selectable(app, test, group.label().c_str());
        if (clicked) {
            group.open = !group.open;
        }
        const bool changed = context_menu_visitor(app, &group);
        ImGui::TableNextColumn(); // spinner for running tests
        ImSpinner::SpinnerIncDots("running", 5, 1);
        ImGui::TableNextColumn(); // enabled / disabled
        // TODO: make this look better
        ImGui::Checkbox("##enabled", &group.enabled);

        if (group.open) {
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
    if (ImGui::BeginTable("tests", 3)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
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
        if (ImGui::Selectable("##element", elem->selected, SELECTABLE_FLAGS, ImVec2(0, 21))) {
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
            std::string text = files.empty() ? "Select Files" : "Selected " + std::to_string(files.size()) + " Files (Hover to see names)";
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

template<typename Data>
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

void editor_tab_test_requests(AppState* app, EditorTab tab, Test& test) noexcept {
    if (ImGui::BeginTabBar("Request")) {
        ImGui::PushID("request");

        if (ImGui::BeginTabItem("Request")) {
            ImGui::Text("Select any of the tabs to edit test's request");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            if (ImGui::Combo(
                    "Body Type", (int*)&test.body_type,
                    RequestBodyTypeLabels, IM_ARRAYSIZE(RequestBodyTypeLabels))) {

                // TODO: convert between current body types
                switch (test.body_type) {
                case REQUEST_JSON:
                    test.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Json());
                    if (!std::holds_alternative<std::string>(test.body)) {
                        test.body = "{}";
                        test.editor.SetText("{}");
                        // TODO: allow for palette change within view settings
                        test.editor.SetPalette(TextEditor::GetDarkPalette());
                    }

                    break;

                case REQUEST_RAW:
                    test.editor.SetLanguageDefinition(TextEditor::LanguageDefinition::Json());
                    if (!std::holds_alternative<std::string>(test.body)) {
                        test.body = "";
                        test.editor.SetText("");
                        // TODO: allow for palette change within view settings
                        test.editor.SetPalette(TextEditor::GetDarkPalette());
                    }
                    break;

                case REQUEST_MULTIPART:
                    test.body = MultiPartBody{};
                    break;
                }
            }

            switch (test.body_type) {
            case REQUEST_JSON:
                ImGui::SameLine();
                if (ImGui::Button("Format")) {
                    try {
                        test.editor.SetText(json::parse(test.editor.GetText()).dump(4));
                    } catch (json::parse_error& error) {
                        Log(LogLevel::Error, (std::string("Failed to parse json for formatting: ") + error.what()).c_str());
                    }
                }

                ImGui::PushFont(app->mono_font);
                test.editor.Render("##body", false, ImVec2(0, 300));
                ImGui::PopFont();

                test.body = test.editor.GetText();
                break;

            case REQUEST_RAW:
                ImGui::PushFont(app->mono_font);
                test.editor.Render("##body", false, ImVec2(0, 300));
                ImGui::PopFont();
                test.body = test.editor.GetText();
                break;

            case REQUEST_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.body);
                partial_dict(app, &mpb, "##body");
                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Parameters")) {
            // TODO: make this unable to add/remove elements and do it automatically by tracking endpoint text changes
            partial_dict(app, &test.parameters, "##parameters");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cookies")) {
            partial_dict(app, &test.cookies, "##cookies");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::InputTextMultiline("##headers", &test.headers, ImVec2(0, 300));
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
        ImGui::PopID();
    }
}

enum EditorTabResult {
    TAB_NONE,
    TAB_CLOSED,
    TAB_SAVED,
};

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab) noexcept {
    assert(std::holds_alternative<Test>(tab.edit));
    auto& test = std::get<Test>(tab.edit);

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(std::visit(LabelVisit(), *tab.original).c_str(), &open, ImGuiTabItemFlags_None)) {
        ImGui::InputText("Endpoint", &test.endpoint);
        ImGui::Combo(
            "Type", (int*)&test.type,
            HTTPTypeLabels, IM_ARRAYSIZE(HTTPTypeLabels));

        editor_tab_test_requests(app, tab, test);

        if (ImGui::Button("Save")) {
            result = TAB_SAVED;
        }

        ImGui::EndTabItem();
    }
    if (!open) {
        result = TAB_CLOSED;
    }
    return result;
}

EditorTabResult editor_tab_group(AppState* app, EditorTab& tab) noexcept {
    assert(std::holds_alternative<Group>(tab.edit));
    auto& group = std::get<Group>(tab.edit);

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            std::visit(LabelVisit(), *tab.original).c_str(), &open,
            tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None)) {
        ImGui::InputText("Name", &group.name);

        if (ImGui::Button("Save")) {
            result = TAB_SAVED;
        }

        ImGui::EndTabItem();
    }
    if (!open) {
        result = TAB_CLOSED;
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
            case TEST_TYPE: {
                result = editor_tab_test(app, tab);
            } break;
            case GROUP_TYPE: {
                result = editor_tab_group(app, tab);
            } break;
            }

            tab.just_opened = false;

            switch (result) {
            case TAB_SAVED:
                *tab.original = tab.edit;
                break;
            // hopefully can't close 2 tabs in a single frame
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

void show_menus(AppState* app) noexcept {
    if (arrow("start", ImGuiDir_Right)) {
        Log(LogLevel::Info, "Started testing");
    }
}

void show_gui(AppState* app) noexcept {
    auto io = ImGui::GetIO();
    ImGui::ShowDemoWindow();
}

void load_fonts(AppState* app) noexcept {
    // TODO: fix log window icons
    app->regular_font = HelloImGui::LoadFont("fonts/DroidSans.ttf", 15, {.useFullGlyphRange = true});
    app->mono_font = HelloImGui::LoadFont("fonts/MesloLGS NF Regular.ttf", 15, {.useFullGlyphRange = true});
}

int main(int argc, char* argv[]) {
    HelloImGui::RunnerParams runner_params;
    httplib::Client cli("");
    auto app = AppState{
        .cli = std::move(cli),
    };

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.defaultImGuiWindowType = HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };
    runner_params.callbacks.LoadAdditionalFonts = [&app]() { load_fonts(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;

    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withMarkdown = true;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/hello_imgui_theme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "immapp/immapp.h"
#include "imspinner/imspinner.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

#include "unordered_map"
#include "variant"

using HelloImGui::Log;
using HelloImGui::LogLevel;

template <class... Ts> struct overloaded : Ts... {
    using Ts::operator()...;
};

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV |
    ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

enum HTTPType : int {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
};

static const char *HTTPTypeLabels[] = {
    [HTTP_GET] = (const char *)"GET",
    [HTTP_POST] = (const char *)"POST",
    [HTTP_PUT] = (const char *)"PUT",
    [HTTP_DELETE] = (const char *)"DELETE",
    [HTTP_PATCH] = (const char *)"PATCH",
};

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
    uint64_t operator()(const auto &idable) const noexcept { return idable.id; }
};

struct LabelVisit {
    const std::string operator()(const auto &labelable) const noexcept {
        return labelable.label();
    }
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
    // keys are ids and values are for separate for editing (must be saved to
    // apply changes)
    struct EditorTab {
        NestedTest *original;
        NestedTest edit;
    };
    std::unordered_map<size_t, EditorTab> opened_editor_tabs = {};
    std::unordered_set<size_t> selected_tests = {};
};

void delete_group(AppState *app, const Group *group) noexcept;
void delete_test(AppState *app, NestedTest test) noexcept;

void delete_group(AppState *app, const Group *group) noexcept {
    for (auto child_id : group->children_idx) {
        auto child = app->tests[child_id];
        delete_test(app, child);
    }
}

void delete_test(AppState *app, NestedTest test) noexcept {
    size_t id;
    size_t parent_id;

    switch (test.index()) {
    case TEST_TYPE: {
        auto &leaf = std::get<Test>(test);

        id = leaf.id;
        parent_id = leaf.parent_id;
    } break;
    case GROUP_TYPE: {
        auto &group = std::get<Group>(test);

        delete_group(app, &group);
        id = group.id;
        parent_id = group.parent_id;
    } break;
    }

    // remove it's id from parents child id list
    auto &parent = std::get<Group>(app->tests.at(parent_id));
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

bool context_menu_visitor(AppState *app, Group *group) noexcept {
    bool change = false;
    if (ImGui::BeginPopupContextItem()) {
        if (!app->selected_tests.contains(group->id)) {
            app->selected_tests.clear();
            app->selected_tests.insert(group->id);
        }
        if (ImGui::MenuItem("Edit")) {
            app->opened_editor_tabs[group->id] = {
                .original = &app->tests[group->id], .edit = *group};
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

bool context_menu_visitor(AppState *app, Test *test) noexcept {
    bool change = false;
    if (ImGui::BeginPopupContextItem()) {
        if (!app->selected_tests.contains(test->id)) {
            app->selected_tests.clear();
            app->selected_tests.insert(test->id);
        }

        if (ImGui::MenuItem("Edit")) {
            app->opened_editor_tabs[test->id] = {
                .original = &app->tests[test->id], .edit = *test};
        }

        if (ImGui::MenuItem("Delete", nullptr, false, test->id != 0)) {
            delete_test(app, *test);
            change = true;
        }
        ImGui::EndPopup();
    }
    return change;
}

bool tree_selectable(AppState *app, NestedTest &test) noexcept {
    ImGuiSelectableFlags selectable_flags =
        ImGuiSelectableFlags_SpanAllColumns |
        ImGuiSelectableFlags_AllowOverlap |
        ImGuiSelectableFlags_AllowDoubleClick;
    const auto id = std::visit(IDVisit(), test);
    bool item_is_selected = app->selected_tests.contains(id);
    if (ImGui::Selectable("##selectable", item_is_selected, selectable_flags,
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

void display_tree_test(AppState *app, NestedTest &test,
                       float indentation = 10) noexcept {
    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const bool ctrl = ImGui::GetIO().KeyCtrl;

    ImGui::PushID(std::visit(IDVisit(), test));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);
    switch (test.index()) {
    case TEST_TYPE: {
        auto &group = std::get<Group>(test);

        ImGui::TableNextColumn(); // test
        const bool clicked = tree_selectable(app, test);
        if (clicked) {
            group.open = !group.open;
        }
        const bool changed = context_menu_visitor(app, &group);
        ImGui::SameLine();
        ImGui::InvisibleButton("", ImVec2(indentation, 10));
        ImGui::SameLine();
        if (group.open) {
            ImGui::Text(ICON_FA_CARET_DOWN " %s", group.name.c_str());
        } else {
            ImGui::Text(ICON_FA_CARET_RIGHT "  %s", group.name.c_str());
        }
        ImGui::TableNextColumn(); // spinner for running tests
        ImSpinner::SpinnerIncDots("running", 5, 1);
        ImGui::TableNextColumn(); // enabled / disabled
        ImGui::Checkbox("##enabled", &group.enabled);

        if (group.open) {
            if (!changed) {
                for (size_t child_id : group.children_idx) {
                    display_tree_test(app, app->tests[child_id],
                                      indentation + 20);
                }
            }
        }
    } break;
    case GROUP_TYPE: {
        auto &leaf = std::get<Test>(test);
        const auto io = ImGui::GetIO();
        ImGui::TableNextColumn(); // test
        const bool double_clicked =
            tree_selectable(app, test) && io.MouseDoubleClicked[0];
        const bool changed = context_menu_visitor(app, &leaf);
        ImGui::SameLine();
        ImGui::InvisibleButton("", ImVec2(indentation, 10));
        ImGui::SameLine();
        ImGui::Text("%s", leaf.endpoint.c_str());
        ImGui::TableNextColumn(); // spinner for running tests
        ImSpinner::SpinnerIncDots("running", 5, 1);
        ImGui::TableNextColumn(); // enabled / disabled
        ImGui::Checkbox("##enabled", &leaf.enabled);

        if (!changed && double_clicked) {
            app->opened_editor_tabs[leaf.id] = {
                .original = &app->tests[leaf.id], .edit = leaf};
        }
    } break;
    }

    ImGui::PopID();
}

void test_tree_view(AppState *app) noexcept {
    if (ImGui::BeginTable("tests", 3)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed,
                                15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed,
                                23.0f);
        display_tree_test(app, app->tests[0]);
        ImGui::EndTable();
    }
}

enum EditorTabResult {
    TAB_NONE,
    TAB_CLOSED,
    TAB_SAVED,
};

EditorTabResult editor_tab_test(AppState *app, const NestedTest *original,
                                Test &test) {
    bool open = true;
    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(std::visit(LabelVisit(), *original).c_str(), &open,
                            ImGuiTabItemFlags_None)) {
        ImGui::InputText("Endpoint", &test.endpoint);
        ImGui::Combo("Type", (int *)&test.type, HTTPTypeLabels,
                     IM_ARRAYSIZE(HTTPTypeLabels));

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

EditorTabResult editor_tab_group(AppState *app, const NestedTest *original,
                                 Group &group) {
    bool open = true;
    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(std::visit(LabelVisit(), *original).c_str(), &open,
                            ImGuiTabItemFlags_None)) {
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

void tabbed_editor(AppState *app) noexcept {
    if (ImGui::BeginTabBar("editor")) {
        size_t closed_id = -1;
        for (auto &[id, tab] : app->opened_editor_tabs) {
            const NestedTest *original = tab.original;
            EditorTabResult result;
            switch (tab.edit.index()) {
            case TEST_TYPE: {
                result =
                    editor_tab_test(app, original, std::get<Test>(tab.edit));
            } break;
            case GROUP_TYPE: {
                result =
                    editor_tab_group(app, original, std::get<Group>(tab.edit));
            } break;
            }

            switch (result) {
            case TAB_SAVED:
                *tab.original = tab.edit;
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
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
    auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace",
                                              ImGuiDir_Down, 0.2);
    auto tests_split = HelloImGui::DockingSplit(
        "MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState *app) noexcept {
    auto tab_editor_window = HelloImGui::DockableWindow(
        "Editor", "MainDockSpace", [app]() { tabbed_editor(app); });

    auto tests_window = HelloImGui::DockableWindow(
        "Tests", "SideBarDockSpace", [app]() { test_tree_view(app); });

    auto logs_window = HelloImGui::DockableWindow(
        "Logs", "LogDockSpace", [app]() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, logs_window};
}

HelloImGui::DockingParams layout(AppState *app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
}

void show_menus(AppState *app) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000022);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000011);
    if (ImGui::ArrowButton("start", ImGuiDir_Right)) {
        Log(LogLevel::Info, "Started testing");
    }
    ImGui::PopStyleColor(3);
}

void show_gui(AppState *app) noexcept {
    auto io = ImGui::GetIO();
    ImGui::ShowDemoWindow();
}

int main(int argc, char *argv[]) {
    HelloImGui::RunnerParams runner_params;
    httplib::Client cli("");
    auto app = AppState{
        .cli = std::move(cli),
    };

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;

    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withMarkdown = true;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

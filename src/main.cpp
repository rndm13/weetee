#include "imgui.h"
#include "imgui_internal.h"
#include "immapp/immapp.h"
#include "imgui_md_wrapper.h"


#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

#include "variant"

#include "cstdio"

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV |
    ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

enum HTTPType {
    HTTP_GET,
    HTTP_POST,
    HTTP_PUT,
    HTTP_DELETE,
    HTTP_PATCH,
};

struct Test;
struct Group;
using NestedTest = std::variant<Test, Group>;
struct Test {
    HTTPType type;
    std::string name;
};
struct Group {
    std::string name;
    bool open;
    std::vector<NestedTest> children;
};

struct AppState {
    httplib::Client cli;
    NestedTest root_group = Group{
        .name = "root",
        .open = true,
        .children = {
            Group{
                .name = "example",
                .children = {
                    Test{ HTTP_GET, "https://example.com", },
                    Test{ HTTP_POST, "https://notexample.com", },
                }
            },
        },
    };
};

void homepage(AppState* state) noexcept {
    ImGui::Text("Hello, this is weetee!");
    ImGui::Text("I'll write more things here soon, maybe.");
}

void group_context(const Group* group) {
    if (ImGui::BeginPopupContextItem()) {
        ImGui::Text("This a popup for group \"%s\"!", group->name.c_str());
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void test_context(const Test* test) {
    if (ImGui::BeginPopupContextItem()) {
        ImGui::Text("This a popup for test \"%s\"!", test->name.c_str());
        if (ImGui::Button("Close"))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }
}

void display_test(const NestedTest* test) {
    ImGui::TableNextRow();
    ImGui::TableNextColumn();
    if (std::holds_alternative<Group>(*test)) {
        const auto group = std::get<Group>(*test);
        bool open = ImGui::TreeNodeEx(group.name.c_str(), ImGuiTreeNodeFlags_UpsideDownArrow | ImGuiTreeNodeFlags_SpanFullWidth);
        group_context(&group);

        if (open) {
            for (const NestedTest& child_test : group.children) {
                display_test(&child_test);
            }
            ImGui::TreePop();
        }
    } else {
        const auto leaf = std::get<Test>(*test);

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImGuiID id = window->GetID(leaf.name.c_str());
        // TODO: figure out how to hide arrow while keeping it double clickable
        bool clicked = ImGui::TreeNodeBehavior(
                id, 
                /* ImGuiTreeNodeFlags_Leaf | /* ImGuiTreeNodeFlags_Bullet | */ 
                ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth, 
                leaf.name.c_str());
        test_context(&leaf);

        if (clicked) {
            // TODO: open details window
            printf("clicked %s!\n", leaf.name.c_str());
            ImGui::TreeNodeSetOpen(id, false);
        }
    }
}

void tests(AppState* state) noexcept {
    if (ImGui::BeginTable("tests", 1)) {
        display_test(&state->root_group);
        ImGui::EndTable();
    }
}

std::vector<HelloImGui::DockingSplit> splits() noexcept { 
    auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.2);
    auto tests_split = HelloImGui::DockingSplit("MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState *state) noexcept {
    auto homepage_window = HelloImGui::DockableWindow(
            "Homepage", "MainDockSpace", [state]() { homepage(state); });

    auto tests_window = HelloImGui::DockableWindow(
            "Tests", "SideBarDockSpace", [state]() { tests(state); });

    auto logs_window = HelloImGui::DockableWindow(
            "Logs", "LogDockSpace", [state]() { HelloImGui::LogGui(); });

    return {tests_window, homepage_window, logs_window};
}

HelloImGui::DockingParams layout(AppState *state) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(state);
    params.dockingSplits = splits();

    return params;
}

void show_gui(AppState *state) noexcept {
    auto io = ImGui::GetIO();
}

int main(int argc, char *argv[]) {
    httplib::Client cli("");
    auto state = AppState{
        .cli = std::move(cli),
    };

    HelloImGui::RunnerParams runner_params;
    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
    runner_params.callbacks.ShowGui = [&state]() { show_gui(&state); };

    runner_params.dockingParams = layout(&state);

    ImmApp::AddOnsParams addOnsParams;
    addOnsParams.withMarkdown = true;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

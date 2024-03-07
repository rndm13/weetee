#include "immapp/immapp.h"
#include "imgui_md_wrapper.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

struct AppState {
    httplib::Client cli;
};

void homepage(AppState* state) noexcept {
    ImGui::Text("Hello, this is weetee!");
}

void tests(AppState* state) noexcept {
    ImGui::Text("Hello, this is tests window!");
}

std::vector<HelloImGui::DockingSplit> splits() noexcept { 
    auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.2);
    auto tests_split = HelloImGui::DockingSplit("MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.15);
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

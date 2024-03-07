#include "immapp/immapp.h"
#include "imgui_md_wrapper.h"


struct AppState {

};

void tests(AppState* state) noexcept {

}

std::vector<HelloImGui::DockingSplit> splits() noexcept { return {}; }

std::vector<HelloImGui::DockableWindow> windows(AppState *state) noexcept {
  auto customers_window = HelloImGui::DockableWindow(
      "Customers", "MainDockSpace", [state]() { tests(state); });


  auto logs_window = HelloImGui::DockableWindow(
      "Logs", "MainDockSpace", [state]() { HelloImGui::LogGui(); });

  return {customers_window, logs_window};
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
  // httplib::Client cli("http://192.168.213.67:8000");
  auto state = AppState{
      // .cli = std::move(cli),
  };

  HelloImGui::RunnerParams runner_params;
  runner_params.appWindowParams.windowTitle = "weetee";
  runner_params.imGuiWindowParams.defaultImGuiWindowType =
      HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;
  runner_params.callbacks.ShowGui = [&state]() { show_gui(&state); };

  runner_params.dockingParams = layout(&state);

  ImmApp::AddOnsParams addOnsParams;
  addOnsParams.withMarkdown = true;
  ImmApp::Run(runner_params, addOnsParams);
  return 0;
}

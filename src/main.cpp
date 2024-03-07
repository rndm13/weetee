#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_md_wrapper.h"
#include "immapp/immapp.h"

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
  Group *parent;
  uint64_t id;

  HTTPType type;
  std::string name;

  const std::string label() noexcept {
    return this->name + "##" + std::to_string(this->id);
  }
};
struct Group {
  Group *parent;
  uint64_t id;

  std::string name;
  bool open;
  std::vector<NestedTest> children;

  const std::string label() noexcept {
    return this->name + "##" + std::to_string(this->id);
  }
};

struct AppState {
  httplib::Client cli;
  uint64_t id_counter = 0;
  NestedTest root_group = Group{
      .parent = nullptr,
      .id = 0,
      .name = "root",
      .open = true,
      .children = {},
  };
};

void homepage(AppState *state) noexcept {
  ImGui::Text("Hello, this is weetee!");
  ImGui::Text("I'll write more things here soon, maybe.");
}

bool context_menu_visitor(AppState *app, Group *group) noexcept {
  bool change = false;
  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::MenuItem("Add a new test")) {
      change = true;
      group->open = true;
      group->children.push_back(Test{
          .parent = group,
          .id = ++app->id_counter,
          .type = HTTP_GET,
          .name = "https:://example.com",
      });
    }
    if (ImGui::MenuItem("Add a new group")) {
      change = true;
      group->open = true;
      group->children.push_back(Group{
          .parent = group,
          .id = ++app->id_counter,
          .name = "New group",
          .children = {},
      });
    }
    if (ImGui::MenuItem("Delete", nullptr, false, group->parent)) {
      for (auto it = group->parent->children.begin();
           it != group->parent->children.end(); it++) {
        if (std::holds_alternative<Group>(*it)) {
          auto it_g = std::get<Group>(*it);
          if (group->id == it_g.id) {
            group->parent->children.erase(it);
            break;
          }
        }
      }
      change = true;
    }
    ImGui::EndPopup();
  }
  return change;
}

bool context_menu_visitor(AppState *app, Test *test) noexcept {
  bool change = false;
  if (ImGui::BeginPopupContextItem()) {
    if (ImGui::MenuItem("Delete", nullptr, false, test->parent)) {
      for (auto it = test->parent->children.begin();
           it != test->parent->children.end(); it++) {
        if (std::holds_alternative<Test>(*it)) {
          auto it_t = std::get<Test>(*it);
          if (test->id == it_t.id) {
            test->parent->children.erase(it);
            break;
          }
        }
      }
      change = true;
    }
    ImGui::EndPopup();
  }
  return change;
}

void display_test(AppState *app, NestedTest &test) noexcept {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  if (std::holds_alternative<Group>(test)) {
    auto &group = std::get<Group>(test);
    bool open = ImGui::TreeNodeEx(group.label().c_str(),
                                  /* ImGuiTreeNodeFlags_UpsideDownArrow | */
                                  ImGuiTreeNodeFlags_SpanFullWidth);
    bool changed = context_menu_visitor(app, &group);
    if (open) {
      if (!changed) {
        for (NestedTest &child_test : group.children) {
          display_test(app, child_test);
        }
      }
      ImGui::TreePop();
    }
  } else {
    auto &leaf = std::get<Test>(test);

    ImGuiWindow *window = ImGui::GetCurrentWindow();
    const ImGuiID id = window->GetID(leaf.label().c_str());
    // TODO: figure out how to hide arrow while keeping it double clickable
    bool clicked = ImGui::TreeNodeBehavior(
        id,
        /* ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_Bullet | */
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanFullWidth,
        leaf.name.c_str());
    bool changed = context_menu_visitor(app, &leaf);
    if (!changed && clicked) {
      // TODO: open details window
      printf("clicked %s!\n", leaf.name.c_str());
      ImGui::TreeNodeSetOpen(id, false);
    }
  }
}

void tests(AppState *app) noexcept {
  if (ImGui::BeginTable("tests", 1)) {
    display_test(app, app->root_group);
    ImGui::EndTable();
  }
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
  auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace",
                                            ImGuiDir_Down, 0.2);
  auto tests_split = HelloImGui::DockingSplit(
      "MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2);
  return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState *state) noexcept {
  auto homepage_window = HelloImGui::DockableWindow(
      "Homepage", "MainDockSpace", [state]() { homepage(state); });

  auto tests_window = HelloImGui::DockableWindow("Tests", "SideBarDockSpace",
                                                 [state]() { tests(state); });

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

void show_gui(AppState *state) noexcept { auto io = ImGui::GetIO(); }

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

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_md_wrapper.h"
#include "immapp/immapp.h"
#include <unordered_map>

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
  size_t parent_id;
  uint64_t id;

  HTTPType type;
  std::string name;

  const std::string label() noexcept {
    return this->name + "##" + std::to_string(this->id);
  }
};
struct Group {
  size_t parent_id;
  uint64_t id;

  std::string name;
  bool open;
  std::vector<size_t> children_idx;

  const std::string label() noexcept {
    return this->name + "##" + std::to_string(this->id);
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
              .open = true,
              .children_idx = {},
          },
      },
  };
};

void homepage(AppState *state) noexcept {
  ImGui::Text("Hello, this is weetee!");
  ImGui::Text("I'll write more things here soon, maybe.");
}

void delete_group(AppState *app, const Group *group) noexcept {
  for (auto child_id : group->children_idx) {
    auto child = app->tests[child_id];
    if (std::holds_alternative<Group>(child)) {
      const Group group_child = std::get<Group>(child);
      delete_group(app, &group_child);
    }

    app->tests.erase(child_id);
  }
  app->tests.erase(group->id);
}

void delete_test(AppState *app, NestedTest test) noexcept {
  size_t id;
  size_t parent_id;
  if (std::holds_alternative<Test>(test)) {
    const Test leaf_test = std::get<Test>(test);
    id = leaf_test.id;
    parent_id = leaf_test.parent_id;
  } else if (std::holds_alternative<Group>(test)) {
    const Group group = std::get<Group>(test);
    delete_group(app, &group);
    id = group.id;
    parent_id = group.parent_id;
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
}

bool context_menu_visitor(AppState *app, Group *group) noexcept {
  bool change = false;
  if (ImGui::BeginPopupContextItem()) {

    if (ImGui::MenuItem("Add a new test")) {
      change = true;
      group->open = true;

      auto id = ++app->id_counter;
      app->tests[id] = (Test{
          .parent_id = group->id,
          .id = id,
          .type = HTTP_GET,
          .name = "https:://example.com",
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
    if (ImGui::MenuItem("Delete", nullptr, false, test->id != 0)) {
      delete_test(app, *test);
      change = true;
    }
    ImGui::EndPopup();
  }
  return change;
}

void display_test(AppState *app, NestedTest &test) noexcept {
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  auto window = ImGui::GetCurrentWindow();
  if (std::holds_alternative<Group>(test)) {
    auto &group = std::get<Group>(test);
    const std::string label = group.label();
    auto id = window->GetID(label.c_str());
    const bool open =
        ImGui::TreeNodeEx(label.c_str(),
                          ImGuiTreeNodeFlags_SpanFullWidth);
    // if (open != group.open) {
    //   ImGui::TreeNodeSetOpen(id, group.open);
    // }
    const bool changed = context_menu_visitor(app, &group);
    if (open) {
      if (!changed) {
        for (size_t child_id : group.children_idx) {
          display_test(app, app->tests[child_id]);
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
    display_test(app, app->tests[0]);
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

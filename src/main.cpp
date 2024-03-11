#include "hello_imgui/hello_imgui_theme.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"
#include "imgui_md_wrapper.h"
#include "immapp/immapp.h"

#define CPPHTTPLIB_OPENSSL_SUPPORT
#include "../external/cpp-httplib/httplib.h"
#include "../external/json-include/json.hpp"

#include "variant"
#include "unordered_map"

#include "cstdio"

template <class... Ts> struct overloaded : Ts... { using Ts::operator()...; };

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV |
    ImGuiTableFlags_Hideable | ImGuiTableFlags_BordersOuter |
    ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

enum HTTPType: int {
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

struct Test;
struct Group;
using NestedTest = std::variant<Test, Group>;
struct Test {
  size_t parent_id;
  uint64_t id;

  HTTPType type;
  std::string endpoint;

  const std::string label() const noexcept {
    return this->endpoint + "##" + std::to_string(this->id);
  }
};

struct Group {
  size_t parent_id;
  uint64_t id;

  std::string name;
  bool open;
  std::vector<size_t> children_idx;

  const std::string label() const noexcept {
    return this->name + "##" + std::to_string(this->id);
  }
};
struct LabelVisit{
  const std::string operator()(const auto& labelable) const noexcept {
    return labelable.label();
  }
};
// const auto label_visit = overloaded{&Test::label, &Group::label};

struct AppState {
  httplib::Client cli;
  uint64_t id_counter = 0;
  std::unordered_map<size_t, NestedTest> tests = {
    {0, Group{
        .parent_id = static_cast<size_t>(-1),
        .id = 0,
        .name = "root",
        .open = true,
        .children_idx = {},
      },
    },
  };
  // keys are ids and values are for separate for editing (must be saved to apply changes)
  struct EditorTab {
    NestedTest* original;
    NestedTest edit;
  };
  std::unordered_map<size_t, EditorTab> opened_editor_tabs = {};
};

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

  std::visit(overloaded{
    [&id, &parent_id](Test test) {
      id = test.id;
      parent_id = test.parent_id;
    },
    [&id, &parent_id, app](Group group) {
      delete_group(app, &group);
      id = group.id;
      parent_id = group.parent_id;
    },
  },
  test);

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
    if (ImGui::MenuItem("Edit")) {
      app->opened_editor_tabs[group->id] = {.original = &app->tests[group->id], .edit = *group};
    }

    if (ImGui::MenuItem("Add a new test")) {
      change = true;
      group->open = true;

      auto id = ++app->id_counter;
      app->tests[id] = (Test{
          .parent_id = group->id,
          .id = id,
          .type = HTTP_GET,
          .endpoint = "https:://example.com",
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
    if (ImGui::MenuItem("Edit")) {
      app->opened_editor_tabs[test->id] = {.original = &app->tests[test->id], .edit = *test};
    }

    if (ImGui::MenuItem("Delete", nullptr, false, test->id != 0)) {
      delete_test(app, *test);
      change = true;
    }
    ImGui::EndPopup();
  }
  return change;
}

void display_tree_test(AppState *app, NestedTest &test) noexcept {
  ImGuiWindow *window = ImGui::GetCurrentWindow();
  ImGui::TableNextRow();
  ImGui::TableNextColumn();
  if (std::holds_alternative<Group>(test)) {
    auto &group = std::get<Group>(test);
    const std::string label = group.label();
    auto id = window->GetID(label.c_str());
    const bool open =
        ImGui::TreeNodeEx(label.c_str(), ImGuiTreeNodeFlags_SpanFullWidth);
    const bool changed = context_menu_visitor(app, &group);
    if (open) {
      if (!changed) {
        for (size_t child_id : group.children_idx) {
          display_tree_test(app, app->tests[child_id]);
        }
      }
      ImGui::TreePop();
    }
  } else {
    auto &leaf = std::get<Test>(test);

    const ImGuiID id = window->GetID(leaf.label().c_str());
    // TODO: figure out how to hide arrow while keeping it double clickable
    bool clicked = ImGui::TreeNodeBehavior(
        id,
        ImGuiTreeNodeFlags_OpenOnDoubleClick |
            ImGuiTreeNodeFlags_NoTreePushOnOpen |
            ImGuiTreeNodeFlags_SpanFullWidth,
        leaf.endpoint.c_str());
    bool changed = context_menu_visitor(app, &leaf);

    if (!changed && clicked) {
      app->opened_editor_tabs[leaf.id] = {.original = &test, .edit = test};
      ImGui::TreeNodeSetOpen(id, false);
    }
  }
}

void homepage(AppState *app) noexcept {
  ImGui::Text("Hello, this is weetee!");
  ImGui::Text("I'll write more things here soon, maybe.");
}

void test_tree_view(AppState *app) noexcept {
  if (ImGui::BeginTable("tests", 1)) {
    display_tree_test(app, app->tests[0]);
    ImGui::EndTable();
  }
}

enum EditorTabResult {
    TAB_NONE,
    TAB_CLOSED,
    TAB_SAVED,
};

EditorTabResult editor_tab_test(AppState *app, const NestedTest* original, Test& test) {
    bool open = true;
    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(std::visit(LabelVisit(), *original).c_str(), &open, ImGuiTabItemFlags_None)) {
        ImGui::InputText("Endpoint", &test.endpoint);
        ImGui::Combo("Type", (int*)&test.type, HTTPTypeLabels, IM_ARRAYSIZE(HTTPTypeLabels));

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

EditorTabResult editor_tab_group(AppState *app, const NestedTest* original, Group& group) {
    bool open = true;
    EditorTabResult result = TAB_NONE;
    if (ImGui::BeginTabItem(std::visit(LabelVisit(), *original).c_str(), &open, ImGuiTabItemFlags_None)) {
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
    for (auto& [id, tab] : app->opened_editor_tabs) {
        const NestedTest* original = tab.original;
      EditorTabResult result = std::visit(overloaded{
        [app, original](Test& test) {return editor_tab_test(app, original, test);},
        [app, original](Group& group) {return editor_tab_group(app, original, group);},
      }, tab.edit);

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
  //   auto homepage_window = HelloImGui::DockableWindow(
  //       "Homepage", "MainDockSpace", 
  //       [app]() { homepage(app); });

  auto tab_editor_window = HelloImGui::DockableWindow(
      "Editor", "MainDockSpace", 
      [app]() { tabbed_editor(app); });

  auto tests_window = HelloImGui::DockableWindow(
      "Tests", "SideBarDockSpace",
      [app]() { test_tree_view(app); });

  auto logs_window = HelloImGui::DockableWindow(
      "Logs", "LogDockSpace",
      [app]() { HelloImGui::LogGui(); });

  return {tests_window, tab_editor_window, logs_window};
}

HelloImGui::DockingParams layout(AppState *app) noexcept {
  auto params = HelloImGui::DockingParams();

  params.dockableWindows = windows(app);
  params.dockingSplits = splits();

  return params;
}

void show_gui(AppState *app) noexcept {
    auto io = ImGui::GetIO();
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

  runner_params.dockingParams = layout(&app);
  runner_params.fpsIdling.enableIdling = false;

  ImmApp::AddOnsParams addOnsParams;
  addOnsParams.withMarkdown = true;
  ImmApp::Run(runner_params, addOnsParams);
  return 0;
}

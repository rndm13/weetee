#include "gui.hpp"

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#include "httplib.h"

#include "app_state.hpp"
#include "http.hpp"
#include "json.hpp"
#include "partial_dict.hpp"
#include "test.hpp"
#include "textinputcombo.hpp"
#include "utils.hpp"

#include "algorithm"
#include "cmath"
#include "cstdint"
#include "optional"
#include "sstream"
#include "string"
#include "utility"
#include "variant"

bool tree_view_context(AppState* app, size_t nested_test_id) noexcept {
    assert(app->tests.contains(nested_test_id));
    bool changed = false; // This also indicates that analysis data is invalid

    if (ImGui::BeginPopupContextItem()) {
        app->tree_view_focused = true;

        if (!app->selected_tests.contains(nested_test_id)) {
            app->selected_tests.clear();
            app->select_with_children(nested_test_id);
        }

        auto analysis = app->select_analysis();

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
                if (!std::holds_alternative<Group>(nt) ||
                    (analysis.same_parent && analysis.parent_id == id) ||
                    app->selected_tests.contains(id)) {
                    continue;
                }

                if (ImGui::MenuItem(std::visit(LabelVisitor(), nt).c_str(), nullptr, false,
                                    !changed)) {
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
            if (ImGui::MenuItem("Paste", "Ctrl + V", false,
                                app->can_paste() && analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);

                app->paste(&selected_group);
            }

            if (ImGui::MenuItem("Add a new test", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;

                auto id = ++app->id_counter;
                app->tests[id] = (Test{
                    .parent_id = selected_group.id,
                    .id = id,
                    .flags = TEST_NONE,
                    .type = HTTP_GET,
                    .endpoint = "https://example.com",
                    .request = {},
                    .response = {},
                    .cli_settings = {},
                });
                selected_group.children_ids.push_back(id);
                app->editor_open_tab(id);
            }

            if (ImGui::MenuItem("Sort", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                app->sort(selected_group);
            }

            if (ImGui::MenuItem("Add a new group", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                auto id = ++app->id_counter;
                app->tests[id] = (Group{
                    .parent_id = selected_group.id,
                    .id = id,
                    .flags = GROUP_NONE,
                    .name = "New group",
                    .cli_settings = {},
                    .children_ids = {},
                });
                selected_group.children_ids.push_back(id);
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

        if (analysis.same_parent && ImGui::MenuItem("Group Selected", nullptr, false,
                                                    !analysis.selected_root && !changed)) {
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


bool tree_view_selectable(AppState* app, size_t id, const char* label) noexcept {
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


bool tree_view_show(AppState* app, Test& test, float indentation) noexcept {
    size_t id = test.id;
    bool changed = false;

    if (app->filtered_tests.contains(id)) {
        return changed;
    }

    ImGui::PushID(static_cast<int32_t>(id));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

    const auto io = ImGui::GetIO();

    ImGui::TableNextColumn(); // test
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
    http_type_button(test.type);
    ImGui::SameLine();
    ImGui::Text("%s", test.endpoint.c_str());

    ImGui::TableNextColumn(); // spinner for running tests

    if (app->test_results.contains(id) && app->test_results.at(id).running.load()) {
        ImSpinner::SpinnerIncDots("running", 5, 1);
    }

    ImGui::TableNextColumn(); // enabled / disabled

    bool pd = app->parent_disabled(id);
    if (pd) {
        ImGui::BeginDisabled();
    }

    bool enabled = !(test.flags & TEST_DISABLED);
    if (ImGui::Checkbox("##enabled", &enabled)) {
        if (!enabled) {
            test.flags |= TEST_DISABLED;
        } else {
            test.flags &= ~TEST_DISABLED;
        }

        app->undo_history.push_undo_history(app);
    }

    if (pd) {
        ImGui::EndDisabled();
    }

    ImGui::TableNextColumn(); // selectable
    const bool double_clicked =
        tree_view_selectable(app, id, ("##" + to_string(test.id)).c_str()) &&
        io.MouseDoubleClicked[0];
    if (!changed && !app->selected_tests.contains(0) &&
        ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
        if (!app->selected_tests.contains(test.id)) {
            app->selected_tests.clear();
            app->select_with_children(test.id);
        }

        ImGui::Text("Moving %zu item(s)", app->selected_tests.size());
        ImGui::SetDragDropPayload("MOVE_SELECTED", &test.id, sizeof(size_t));
        ImGui::EndDragDropSource();
    }

    if (!app->selected_tests.contains(test.id) && ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("MOVE_SELECTED")) {
            changed = true;

            app->move(&std::get<Group>(app->tests[test.parent_id]));
        }
        ImGui::EndDragDropTarget();
    }

    changed = changed | tree_view_context(app, id);

    if (!changed && double_clicked) {
        app->editor_open_tab(test.id);
    }

    ImGui::PopID();

    return changed;
}

bool tree_view_show(AppState* app, Group& group, float indentation) noexcept {
    size_t id = group.id;
    bool changed = false;

    if (app->filtered_tests.contains(id)) {
        return changed;
    }

    ImGui::PushID(static_cast<int32_t>(id));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

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

    if (app->test_results.contains(id) && app->test_results.at(id).running.load()) {
        ImSpinner::SpinnerIncDots("running", 5, 1);
    }

    ImGui::TableNextColumn(); // enabled / disabled

    bool pd = app->parent_disabled(id);
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
    const bool clicked = tree_view_selectable(app, id, ("##" + to_string(group.id)).c_str());
    if (clicked) {
        group.flags ^= GROUP_OPEN; // toggle
    }

    if (!changed && !app->selected_tests.contains(0) &&
        ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
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

    changed |= tree_view_context(app, id);

    if (!changed && group.flags & GROUP_OPEN) {
        for (size_t child_id : group.children_ids) {
            assert(app->tests.contains(child_id));
            changed = changed | tree_view_show(app, app->tests.at(child_id), indentation + 22);
            if (changed) {
                break;
            }
        }
    }

    ImGui::PopID();

    return changed;
}

bool tree_view_show(AppState* app, NestedTest& nt, float indentation) noexcept {
    return std::visit(
        [app, indentation](auto& val) { return tree_view_show(app, val, indentation); }, nt);
}

void tree_view(AppState* app) noexcept {
    app->tree_view_focused = ImGui::IsWindowFocused();

    ImGui::PushFont(app->regular_font);

    ImGui::SetNextItemWidth(-1);
    bool changed = ImGui::InputText("##tree_view_search", &app->tree_view_filter);

    if (ImGui::BeginTable("tests", 4)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
        ImGui::TableSetupColumn("selectable", ImGuiTableColumnFlags_WidthFixed, 0.0f);
        changed = changed | tree_view_show(app, app->tests[0], 0.0f);
        ImGui::EndTable();
    }

    if (changed) {
        app->filter(&app->tests[0]);
    }

    ImGui::PopFont();
}

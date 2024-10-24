#include "gui.hpp"

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#include "app_state.hpp"
#include "http.hpp"
#include "json.hpp"
#include "partial_dict.hpp"
#include "tests.hpp"
#include "textinputcombo.hpp"
#include "utils.hpp"

#include "cmath"
#include "cstdint"
#include "optional"
#include "sstream"
#include "string"
#include "utility"
#include "variant"
#include <filesystem>
#include <fstream>
#include <string>

void begin_transparent_button() noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_Border, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
}

void end_transparent_button() noexcept { ImGui::PopStyleColor(5); }

bool arrow(const char* label, ImGuiDir dir) noexcept {
    assert(label);

    begin_transparent_button();
    bool result = ImGui::ArrowButton(label, dir);
    end_transparent_button();

    return result;
}

bool http_type_button(HTTPType type, ImVec2 size) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HTTPTypeColor[type]);

    ImGuiContext& g = *GImGui;
    float backup_padding_y = g.Style.FramePadding.y;
    g.Style.FramePadding.y = 0.0f;
    bool pressed = ImGui::ButtonEx(HTTPTypeLabels[type], size, ImGuiButtonFlags_AlignTextBaseLine);
    g.Style.FramePadding.y = backup_padding_y;

    ImGui::PopStyleColor(3);
    return pressed;
}

template <class... Args> void tooltip(const char* format, Args... args) noexcept {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(format, args...);
    }
}

std::string load_from_file() noexcept {
    auto open_file_dialog = pfd::open_file("Open File", ".", {"All Files", "*"}, pfd::opt::none);

    std::vector<std::string> result = open_file_dialog.result();

    if (result.size() > 0) {
        std::ifstream in(result.at(0));
        if (in) {
            std::stringstream ss;
            ss << in.rdbuf();
            return ss.str();
        }
    }

    return "";
}

template <class... Args> bool hint(const char* format, Args... args) noexcept {
    bool result = ImGui::Button(ICON_FA_QUESTION);
    tooltip(format, args...);
    return result;
}

bool tree_view_context(AppState* app, size_t nested_test_id) noexcept {
    assert(app->tests.contains(nested_test_id));
    bool changed = false; // This also indicates that previous analysis data is no longer valid

    if (ImGui::BeginPopupContextItem("##tree_view_context")) {
        app->tree_view.window_focused = true;

        if (!app->tree_view.selected_tests.contains(nested_test_id)) {
            app->tree_view.selected_tests.clear();
            app->select_with_children(nested_test_id);
        }

        auto analysis = app->select_analysis();

        if (ImGui::MenuItem(app->i18n.tv_edit.c_str(), "Enter", false,
                            analysis.top_selected_count == 1 && !changed)) {
            app->editor_open_tab(nested_test_id);
        }

        if (ImGui::MenuItem(app->i18n.tv_delete.c_str(), "Delete", false,
                            !analysis.selected_root && !changed)) {
            changed = true;

            app->delete_selected();
        }

        if (ImGui::MenuItem(app->i18n.tv_enable.c_str(), nullptr, false, !changed)) {
            changed = true;

            app->enable_selected(true);
        }

        if (ImGui::MenuItem(app->i18n.tv_disable.c_str(), nullptr, false, !changed)) {
            changed = true;

            app->enable_selected(false);
        }

        if (ImGui::BeginMenu(app->i18n.tv_move.c_str(), !changed && !analysis.selected_root)) {
            for (auto& [id, nt] : app->tests) {
                // skip if not a group or same parent for selected or selected group
                if (!std::holds_alternative<Group>(nt) ||
                    (analysis.same_parent && analysis.parent_id == id) ||
                    app->tree_view.selected_tests.contains(id)) {
                    continue;
                }

                if (ImGui::MenuItem(std::visit(LabelVisitor(), nt).c_str(), nullptr, false,
                                    !changed)) {
                    changed = true;

                    assert(std::holds_alternative<Group>(nt));
                    app->move(&std::get<Group>(nt), 0);
                }
            }

            ImGui::EndMenu();
        }

        if (ImGui::MenuItem(app->i18n.tv_copy.c_str(), "Ctrl + C", false, !changed)) {
            app->copy();
        }

        if (ImGui::MenuItem(app->i18n.tv_cut.c_str(), "Ctrl + X", false,
                            !analysis.selected_root && !changed)) {
            changed = true;

            app->cut();
        }

        if (ImGui::MenuItem(app->i18n.tv_paste.c_str(), "Ctrl + V", false,
                            app->can_paste() && !changed)) {
            changed = true;

            NestedTest* nested_test = &app->tests.at(nested_test_id);
            if (std::holds_alternative<Group>(*nested_test)) {
                Group& selected_group = std::get<Group>(*nested_test);

                app->paste(&selected_group);
                app->select_with_children(selected_group.id);
            } else {
                assert(app->tests.contains(std::visit(ParentIDVisitor(), *nested_test)));
                assert(std::holds_alternative<Group>(
                    app->tests.at(std::visit(ParentIDVisitor(), *nested_test))));
                Group& parent_group =
                    std::get<Group>(app->tests.at(std::visit(ParentIDVisitor(), *nested_test)));

                app->paste(&parent_group);
            }
        }

        // only groups without tests
        if (analysis.group && !analysis.test) {
            if (ImGui::MenuItem(app->i18n.tv_sort.c_str(), nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                app->sort(selected_group);
            }

            if (ImGui::MenuItem(app->i18n.tv_new_test.c_str(), nullptr, false,
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

            if (ImGui::MenuItem(app->i18n.tv_new_group.c_str(), nullptr, false,
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
                    .name = app->i18n.tv_new_group_name,
                    .cli_settings = {},
                    .children_ids = {},
                });
                selected_group.children_ids.push_back(id);
            }

            if (ImGui::MenuItem(app->i18n.tv_ungroup.c_str(), nullptr, false,
                                !analysis.selected_root && !changed)) {
                changed = true;

                for (auto selected_id : app->select_top_layer()) {
                    auto& selected = app->tests[selected_id];

                    assert(std::holds_alternative<Group>(selected));
                    auto& selected_group = std::get<Group>(selected);

                    app->move_children_up(&selected_group);
                    app->delete_test(selected_id);
                }
            }
        }

        if (analysis.same_parent) {
            if (ImGui::MenuItem(app->i18n.tv_group.c_str(), nullptr, false,
                                !analysis.selected_root && !changed)) {
                changed = true;

                app->group_selected(analysis.parent_id);
            }
        }

        if (ImGui::MenuItem(app->i18n.tv_run_tests.c_str(), nullptr, false, !changed)) {
            std::vector<size_t> tests_to_run = get_tests_to_run(
                app, app->tree_view.selected_tests.begin(), app->tree_view.selected_tests.end());

            run_tests(app, tests_to_run);
        }

        ImGui::EndPopup();
    }

    if (changed) {
        app->tree_view.selected_tests.clear();
    }

    return changed;
}

bool tree_view_selectable(AppState* app, size_t id, const char* label) noexcept {
    bool item_is_selected = app->tree_view.selected_tests.contains(id);

    bool clicked = false;
    if (ImGui::Selectable(label, item_is_selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
        auto io = ImGui::GetIO();
        if (io.KeyCtrl) {
            if (item_is_selected) {
                app->select_with_children<false>(id);
            } else {
                app->select_with_children(id);
            }
        } else if (io.KeyShift) {
            app->tree_view.selected_tests.clear();

            bool selection_start = false;
            size_t it_group_id = 0, it_idx = 0;
            const Group* root = app->root_group();
            while (it_group_id != root->parent_id && !root->children_ids.empty()) {
                assert(app->tests.contains(it_group_id));
                NestedTest* it_group_nt = &app->tests.at(it_group_id);

                assert(std::holds_alternative<Group>(*it_group_nt));
                Group* it_group = &std::get<Group>(*it_group_nt);

                assert(it_idx < it_group->children_ids.size());
                assert(app->tests.contains(it_group->children_ids.at(it_idx)));

                NestedTest* it_nt = &app->tests.at(it_group->children_ids.at(it_idx));

                size_t it_id = std::visit(IDVisitor(), *it_nt);

                if (it_id == app->tree_view.last_selected_idx || it_id == id) {
                    selection_start = !selection_start;
                }

                if (selection_start || it_id == app->tree_view.last_selected_idx || it_id == id) {
                    app->select_with_children<true>(it_id);
                } else {
                    app->select_with_children<false>(it_id);
                }

                iterate_over_nested_children(app, &it_group_id, &it_idx, root->parent_id);
            }
        } else {
            app->tree_view.selected_tests.clear();

            app->select_with_children(id);
        }

        app->tree_view.last_selected_idx = id;

        clicked = true;
    }

    return clicked;
}

bool tree_view_dnd_target(AppState* app, size_t nested_test_id, size_t idx) noexcept {
    bool changed = false;
    // Don't allow move into itself
    if (app->tree_view.selected_tests.contains(nested_test_id)) {
        return changed;
    }

    if (ImGui::BeginDragDropTarget()) {
        if (ImGui::AcceptDragDropPayload("MOVE_SELECTED")) {
            changed = true;

            assert(app->tests.contains(nested_test_id));
            app->move(&std::get<Group>(app->tests.at(nested_test_id)), idx);
        }
        ImGui::EndDragDropTarget();
    }
    return changed;
}

bool tree_view_dnd_target_row(AppState* app, size_t nested_test_id, size_t idx,
                              float indentation) noexcept {
    ImGui::TableNextRow(ImGuiTableRowFlags_None, 5);
    ImGui::PushID(static_cast<int32_t>(nested_test_id));
    ImGui::TableNextColumn();

    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Horizontal, 5.0f);

    bool changed = tree_view_dnd_target(app, nested_test_id, idx);
    ImGui::PopID();

    return changed;
}

bool vec2_intersect(ImVec2 point, ImVec2 min, ImVec2 max) noexcept {
    return point.x > min.x && point.x < max.x && point.y > min.y && point.y < max.y;
}

bool show_tree_view_row(AppState* app, NestedTest& nt, ImVec2& min, ImVec2& max, size_t idx,
                    float indentation) noexcept {
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, 0});
    auto visitor = [app, &min, &max, idx, indentation](auto& val) {
        return show_tree_view_row(app, val, min, max, idx, indentation);
    };
    bool changed = std::visit(visitor, nt);
    ImGui::PopStyleVar();
    return changed;
}

bool show_tree_view_row(AppState* app, Test& test, ImVec2& min, ImVec2& max, size_t idx,
                    float indentation) noexcept {
    size_t id = test.id;
    bool changed = false;

    if (app->tree_view.filtered_tests.contains(id)) {
        return changed;
    }

    ImGui::PushID(static_cast<int32_t>(id));

    ImGui::TableNextRow(ImGuiTableRowFlags_None, 0);

    const auto io = ImGui::GetIO();

    ImGui::TableNextColumn(); // test
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indentation);
    http_type_button(test.type, ImVec2(0, 0));
    ImGui::SameLine();
    ImGui::Text("%s", test.endpoint.c_str());

    ImGui::TableNextColumn(); // spinner for running tests

    if (is_test_running(app, id)) {
        ImSpinner::SpinnerIncDots("running", 5, 1);
    }

    ImGui::TableNextColumn(); // enabled / disabled

    ImGui::BeginDisabled(app->parent_disabled(id));

    bool enabled = !(test.flags & TEST_DISABLED);
    if (ImGui::Checkbox("##enabled", &enabled)) {
        if (!enabled) {
            test.flags |= TEST_DISABLED;
        } else {
            test.flags &= ~TEST_DISABLED;
        }

        app->undo_history.push_undo_history(app);
    }

    ImGui::EndDisabled();

    ImGui::TableNextColumn(); // selectable
    const bool double_clicked =
        tree_view_selectable(app, id, ("###" + to_string(test.id)).c_str()) &&
        io.MouseDoubleClicked[0];

    if (!changed && !app->tree_view.selected_tests.contains(0) &&
        ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
        if (!app->tree_view.selected_tests.contains(test.id)) {
            app->tree_view.selected_tests.clear();
            app->select_with_children(test.id);
        }

        ImGui::Text("Moving %zu item(s)", app->tree_view.selected_tests.size());
        ImGui::SetDragDropPayload("MOVE_SELECTED", &test.id, sizeof(size_t));
        ImGui::EndDragDropSource();
    }

    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    min.x -= 40;
    min.y -= 40;
    max.x += 40;
    max.y += 40;

    changed |= tree_view_dnd_target(app, test.parent_id, idx);

    changed |= tree_view_context(app, id);

    if (!changed && double_clicked) {
        app->editor_open_tab(test.id);
    }

    ImGui::PopID();

    return changed;
}

bool show_tree_view_row(AppState* app, Group& group, ImVec2& min, ImVec2& max, size_t idx,
                    float indentation) noexcept {
    size_t id = group.id;
    bool changed = false;

    if (app->tree_view.filtered_tests.contains(id)) {
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
    ImGui::Text("%s", group.name.c_str());
    ImGui::SameLine();

    // Add test button
    begin_transparent_button();

    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (ImGui::Button(ICON_FA_PLUS_CIRCLE "##add")) {
        changed = true;

        group.flags |= GROUP_OPEN;

        auto id = ++app->id_counter;
        app->tests[id] = Test{
            .parent_id = group.id,
            .id = id,
            .flags = TEST_NONE,
            .type = HTTP_GET,
            .endpoint = "https://example.com",
            .request = {},
            .response = {},
            .cli_settings = {},
        };

        group.children_ids.push_back(id);
        app->editor_open_tab(id);
    }
    ImGui::PopStyleColor(1);

    end_transparent_button();

    ImGui::TableNextColumn(); // Spinner

    if (is_test_running(app, id)) {
        ImSpinner::SpinnerIncDots("running", 5, 1);
    }

    ImGui::TableNextColumn(); // enabled / disabled

    ImGui::BeginDisabled(app->parent_disabled(id));

    bool enabled = !(group.flags & GROUP_DISABLED);
    if (ImGui::Checkbox("##enabled", &enabled)) {
        if (!enabled) {
            group.flags |= GROUP_DISABLED;
        } else {
            group.flags &= ~GROUP_DISABLED;
        }

        app->undo_history.push_undo_history(app);
    }

    ImGui::EndDisabled();

    ImGui::TableNextColumn(); // selectable
    const bool clicked = tree_view_selectable(app, id, ("###" + to_string(group.id)).c_str());

    auto& io = ImGui::GetIO();
    if (clicked && !io.KeyShift && !io.KeyCtrl) {
        group.flags ^= GROUP_OPEN; // toggle
    }

    if (!changed && !app->tree_view.selected_tests.contains(0) &&
        ImGui::BeginDragDropSource(DRAG_SOURCE_FLAGS)) {
        if (!app->tree_view.selected_tests.contains(group.id)) {
            app->tree_view.selected_tests.clear();
            app->select_with_children(group.id);
        }

        ImGui::Text("Moving %zu item(s)", app->tree_view.selected_tests.size());
        ImGui::SetDragDropPayload("MOVE_SELECTED", nullptr, 0);
        ImGui::EndDragDropSource();
    }

    min = ImGui::GetItemRectMin();
    max = ImGui::GetItemRectMax();
    min.x -= 40;
    min.y -= 40;
    max.x += 40;
    max.y += 40;

    changed |= tree_view_dnd_target(app, group.id, 0);

    changed |= tree_view_context(app, id);

    static constexpr float INDENTATION_INCREMENT = 22;

    ImVec2 mouse = ImGui::GetIO().MousePos;
    if (!changed && group.flags & GROUP_OPEN) {
        size_t index = 1;
        for (size_t child_id : group.children_ids) {
            assert(app->tests.contains(child_id));

            changed |= show_tree_view_row(app, app->tests.at(child_id), min, max, index,
                                      indentation + INDENTATION_INCREMENT);

            if (changed) {
                break;
            }

            index += 1;
        }

        // after the opened group
        if (group.id != 0 && ImGui::IsDragDropActive() && vec2_intersect(mouse, min, max) &&
            !app->tree_view.selected_tests.contains(id)) {
            changed |= tree_view_dnd_target_row(app, group.parent_id, idx, indentation);
        }
    }

    ImGui::PopID();

    return changed;
}

void tree_view(AppState* app) noexcept {
    app->tree_view.window_focused = ImGui::IsWindowFocused();

    ImGui::PushFont(app->regular_font);

    ImGui::SetNextItemWidth(-1);
    bool changed_search = ImGui::InputText("##tree_view_search", &app->tree_view.filter);
    bool changed_data = false;

    if (ImGui::BeginTable("tests", 4)) {
        ImGui::TableSetupColumn("test");
        ImGui::TableSetupColumn("spinner", ImGuiTableColumnFlags_WidthFixed, 15.0f);
        ImGui::TableSetupColumn("enabled", ImGuiTableColumnFlags_WidthFixed, 23.0f);
        ImGui::TableSetupColumn("selectable", ImGuiTableColumnFlags_WidthFixed, 0.0f);

        ImVec2 min;
        ImVec2 max;
        changed_data |= show_tree_view_row(app, app->tests[0], min, max, 0, 0.0f);

        ImGui::EndTable();
    }

    if (changed_data) {
        app->undo_history.push_undo_history(app);
    }

    if (changed_search || changed_data) {
        app->filter(&app->tests[0]);
    }

    ImGui::PopFont();
}

bool partial_dict_data_row(AppState* app, Cookies*, CookiesElement* elem,
                           const VariablesMap& vars) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(vars, elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Parameters*, ParametersElement* elem,
                           const VariablesMap& vars) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(vars, elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Headers*, HeadersElement* elem,
                           const VariablesMap& vars) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(vars, elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Variables* vars_pd, VariablesElement* elem,
                           const VariablesMap& vars) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        ImGui::SetNextItemWidth(-1);
        if (elem->data.separator == '\n') {
            changed |= ImGui::InputTextMultiline("##data", &elem->data.data);
        } else {
            changed |= ImGui::InputText("##data", &elem->data.data);
        }

        if (ImGui::BeginPopupContextItem("##partial_dict_data_context")) {
            partial_dict_data_context(app, vars_pd, elem, vars);
            ImGui::EndPopup();
        }

        tooltip("%s", replace_variables(vars, elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_context(AppState* app, Variables*, VariablesElement* elem,
                               const VariablesMap& vars) noexcept {
    bool changed = false;

    if (ImGui::BeginMenu("Fuzzing Separator")) {
        if (ImGui::MenuItem("None", nullptr, elem->data.separator == std::nullopt)) {
            changed = true;

            find_and_replace(elem->data.data, "\n", " ");

            elem->data.separator = std::nullopt;
        }

        if (ImGui::MenuItem("New Line", nullptr, elem->data.separator == '\n')) {
            changed = true;

            if (elem->data.separator.has_value()) {
                find_and_replace(elem->data.data, std::string{elem->data.separator.value()}, "\n");
            }

            elem->data.separator = '\n';
        }

        char separator[2] = "";
        if (elem->data.separator.has_value() && elem->data.separator != '\n') {
            separator[0] = elem->data.separator.value();
            separator[1] = '\0';
        }

        if (ImGui::InputText("##separator", separator, ARRAY_SIZE(separator))) {
            changed = true;

            if (elem->data.separator.has_value()) {
                find_and_replace(elem->data.data, std::string{elem->data.separator.value()},
                                 separator);
            }

            elem->data.separator = *separator;
        }

        ImGui::EndMenu();
    }

    if (ImGui::MenuItem(ICON_FA_FILE " Load from file")) {
        changed = true;
        elem->data.data = load_from_file();

        if (elem->data.data.find("\n") != std::string::npos) {
            elem->data.separator = '\n';
        }
    }

    return changed;
}

bool partial_dict_data_row(AppState* app, MultiPartBody*, MultiPartBodyElement* elem,
                           const VariablesMap& vars) noexcept {
    bool changed = false;

    if (ImGui::TableNextColumn()) { // type
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##type", app->i18n.ed_mpbd_types[elem->data.type].c_str())) {
            for (size_t i = 0; i < app->i18n.ed_mpbd_types.size(); i++) {
                if (ImGui::Selectable(app->i18n.ed_mpbd_types[i].c_str(), i == elem->data.type)) {
                    elem->data.type = static_cast<MultiPartBodyDataType>(i);

                    switch (elem->data.type) {
                    case MPBD_TEXT:
                        elem->data.data = "";
                        elem->data.content_type = "text/plain";
                        break;
                    case MPBD_FILES:
                        elem->data.data = std::vector<std::string>{};
                        elem->data.content_type = "";
                        break;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }

    if (ImGui::TableNextColumn()) { // body
        switch (elem->data.type) {
        case MPBD_TEXT: {
            assert(std::holds_alternative<std::string>(elem->data.data));
            auto str = &std::get<std::string>(elem->data.data);

            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##text", str)) {
                changed = true;

                elem->data.resolve_content_type();
            }

            tooltip("%s", replace_variables(vars, *str).c_str());
        } break;
        case MPBD_FILES:
            assert(std::holds_alternative<std::vector<std::string>>(elem->data.data));
            auto& files = std::get<std::vector<std::string>>(elem->data.data);
            std::string text = files.empty() ? ICON_FA_FILE " Select Files"
                                                            "###files"
                                             : ICON_FA_FILE " Selected " + to_string(files.size()) +
                                                   " Files (Hover to see names)"
                                                   "###files";
            if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) {
                auto open_file =
                    pfd::open_file("Select Files", ".", {"All Files", "*"}, pfd::opt::multiselect);

                auto result_files = open_file.result();
                if (result_files.size() > 0) {
                    changed = true;

                    elem->data.data = result_files;
                    elem->data.resolve_content_type();
                }
            }

            if (!files.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                std::stringstream ss;
                for (auto& file : files) {
                    ss << file << '\n';
                }
                ImGui::SetTooltip("%s", ss.str().c_str());
            }
            break;
        }
    }

    if (ImGui::TableNextColumn()) { // content-type
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputText("##content_type", &elem->data.content_type);
        tooltip("%s", replace_variables(vars, elem->data.content_type).c_str());
    }
    return changed;
}

bool editor_test_request(AppState* app, Test& test) noexcept {
    const VariablesMap& vars = app->get_test_variables(test.id);

    bool changed = false;

    if (ImGui::BeginTabBar("request")) {
        ImGui::PushID("request");

        if (ImGui::BeginTabItem(app->i18n.ed_rq_request.c_str())) {
            ImGui::Text("%s", app->i18n.ed_rq_request_text.c_str());
            ImGui::EndTabItem();
        }

        if (test.type != HTTP_GET && ImGui::BeginTabItem(app->i18n.ed_rq_body.c_str())) {
            bool body_type_changed = false;
            if (ImGui::BeginCombo(app->i18n.ed_rq_body_type.c_str(),
                                  app->i18n.ed_rq_body_types[test.request.body_type].c_str())) {
                for (size_t i = 0; i < app->i18n.ed_rq_body_types.size(); i++) {
                    if (ImGui::Selectable(app->i18n.ed_rq_body_types[i].c_str(),
                                          i == test.request.body_type)) {
                        body_type_changed = true;
                        test.request.body_type = static_cast<RequestBodyType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            if (body_type_changed) {
                changed = true;

                switch (test.request.body_type) {
                case REQUEST_JSON:
                    request_body_convert<REQUEST_JSON>(&test);
                    break;
                case REQUEST_PLAIN:
                    request_body_convert<REQUEST_PLAIN>(&test);
                    break;
                case REQUEST_MULTIPART:
                    request_body_convert<REQUEST_MULTIPART>(&test);
                    break;
                case REQUEST_OTHER:
                    request_body_convert<REQUEST_OTHER>(&test);
                    break;
                }
            }

            if (test.request.body_type == REQUEST_OTHER) {
                ImGui::InputText("Content-Type", &test.request.other_content_type);
            }

            switch (test.request.body_type) {
            case REQUEST_JSON:
            case REQUEST_PLAIN:
            case REQUEST_OTHER: {
                ImGui::PushFont(app->mono_font);

                std::string* body = &std::get<std::string>(test.request.body);
                changed |= ImGui::InputTextMultiline("##body", body, ImVec2(0, 300));

                if (ImGui::BeginPopupContextItem("##body_json_context")) {
                    if (ImGui::MenuItem("Format JSON")) {
                        assert(std::holds_alternative<std::string>(test.request.body));
                        const char* error =
                            json_format_variables(std::get<std::string>(test.request.body), vars);

                        if (error) {
                            Log(LogLevel::Error, "Failed to parse JSON: ", error);
                        }
                    }

                    if (ImGui::MenuItem(ICON_FA_FILE " Load from file")) {
                        changed = true;
                        test.request.body = load_from_file();
                    }
                    ImGui::EndPopup();
                }

                tooltip("%s", replace_variables(vars, *body).c_str());

                ImGui::PopFont();
            } break;

            case REQUEST_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.request.body);
                changed |= partial_dict(app, &mpb, "body", vars);
                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rq_params.c_str())) {
            ImGui::PushFont(app->mono_font);
            changed |= partial_dict(app, &test.request.parameters, "parameters", vars);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rq_cookies.c_str())) {
            ImGui::PushFont(app->mono_font);
            changed |= partial_dict(app, &test.request.cookies, "cookies", vars);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rq_headers.c_str())) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.request.headers, "headers", vars,
                                             PARTIAL_DICT_NONE, RequestHeadersLabels,
                                             ARRAY_SIZE(RequestHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

bool editor_test_response(AppState* app, Test& test) noexcept {
    const VariablesMap& vars = app->get_test_variables(test.id);

    bool changed = false;

    if (ImGui::BeginTabBar("response")) {
        ImGui::PushID("response");

        if (ImGui::BeginTabItem(app->i18n.ed_rs_response.c_str())) {
            static ComboFilterState s{};
            ComboFilter(app->i18n.ed_rs_status.c_str(), &test.response.status, HTTPStatusLabels,
                        ARRAY_SIZE(HTTPStatusLabels), &s);
            ImGui::Text("%s", app->i18n.ed_rs_response_text.c_str());
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rs_body.c_str())) {
            if (ImGui::BeginCombo(app->i18n.ed_rs_body_type.c_str(),
                                  app->i18n.ed_rs_body_types[test.response.body_type].c_str())) {
                for (size_t i = 0; i < app->i18n.ed_rs_body_types.size(); i++) {
                    if (ImGui::Selectable(app->i18n.ed_rs_body_types[i].c_str(),
                                          i == test.response.body_type)) {
                        changed = true;
                        test.response.body_type = static_cast<ResponseBodyType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            if (test.response.body_type == RESPONSE_OTHER) {
                ImGui::InputText("Content-Type", &test.response.other_content_type);
            }

            switch (test.response.body_type) {
            case RESPONSE_ANY:
            case RESPONSE_JSON:
            case RESPONSE_HTML:
            case RESPONSE_PLAIN:
            case RESPONSE_OTHER:
                ImGui::PushFont(app->mono_font);

                changed |= ImGui::InputTextMultiline("##body", &test.response.body, ImVec2(0, 300));

                if (ImGui::BeginPopupContextItem("##body_json_context")) {
                    if (ImGui::MenuItem("Format JSON")) {
                        const char* error = json_format_variables(test.response.body, vars);

                        if (error) {
                            Log(LogLevel::Error, "Failed to parse JSON: ", error);
                        }
                    }

                    if (ImGui::MenuItem(ICON_FA_FILE " Load from file")) {
                        changed = true;
                        test.response.body = load_from_file();
                    }

                    ImGui::EndPopup();
                }

                tooltip("%s", replace_variables(vars, test.response.body).c_str());

                ImGui::PopFont();

                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rs_set_cookies.c_str())) {
            ImGui::PushFont(app->mono_font);
            changed |= partial_dict(app, &test.response.cookies, "cookies", vars);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem(app->i18n.ed_rs_headers.c_str())) {
            ImGui::PushFont(app->mono_font);
            changed |= partial_dict(app, &test.response.headers, "headers", vars, PARTIAL_DICT_NONE,
                                    ResponseHeadersLabels, ARRAY_SIZE(ResponseHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

ModalResult unsaved_changes(AppState*) noexcept {
    if (!ImGui::IsPopupOpen("Unsaved Changes")) {
        ImGui::OpenPopup("Unsaved Changes");
    }

    ModalResult result = MODAL_NONE;
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr)) {
        ImGui::TextColored(HTTPTypeColor[HTTP_DELETE], "WARNING");
        ImGui::Text("You are about to lose unsaved changes");

        if (ImGui::Button(ICON_FA_CHECK " Continue")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CONTINUE;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_SAVE " Save")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_SAVE;
        }
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_STOP " Cancel")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CANCEL;
        }
        ImGui::EndPopup();
    }

    return result;
}

bool editor_auth(const std::string& label, const I18N* i18n, AuthVariant* auth) noexcept {
    std::string idless_label = label.substr(0, label.find("#"));
    bool changed = false;
    COMBO_VARIANT(label.c_str(), *auth, changed, AuthTypeLabels, AuthVariant);
    switch (auth->index()) {
    case AUTH_NONE:
        break;
    case AUTH_BASIC: {
        assert(std::holds_alternative<AuthBasic>(*auth));
        AuthBasic* basic = &std::get<AuthBasic>(*auth);
        changed |=
            ImGui::InputText((idless_label + " " + i18n->ed_cli_auth_name).c_str(), &basic->name);
        changed |= ImGui::InputText((idless_label + " " + i18n->ed_cli_auth_password).c_str(),
                                    &basic->password, ImGuiInputTextFlags_Password);
    } break;
    case AUTH_BEARER_TOKEN: {
        assert(std::holds_alternative<AuthBearerToken>(*auth));
        AuthBearerToken* token = &std::get<AuthBearerToken>(*auth);
        changed |=
            ImGui::InputText((idless_label + " " + i18n->ed_cli_auth_token).c_str(), &token->token);
    } break;
    }

    return changed;
}

bool editor_client_settings(const I18N* i18n, ClientSettings* set, bool enable_dynamic) noexcept {
    assert(set);

    bool changed = false;

    ImGui::BeginDisabled(!enable_dynamic);

    CHECKBOX_FLAG(set->flags, changed, CLIENT_DYNAMIC, i18n->ed_cli_dynamic.c_str());

    ImGui::SameLine();
    hint(i18n->ed_cli_dynamic_hint.c_str());

    if (set->flags & CLIENT_DYNAMIC) {
        ImGui::SameLine();
        CHECKBOX_FLAG(set->flags, changed, CLIENT_KEEP_ALIVE, i18n->ed_cli_keep_alive.c_str());
    }

    ImGui::EndDisabled();

    CHECKBOX_FLAG(set->flags, changed, CLIENT_COMPRESSION, i18n->ed_cli_compression.c_str());

    CHECKBOX_FLAG(set->flags, changed, CLIENT_FOLLOW_REDIRECTS, i18n->ed_cli_redirects.c_str());
    changed |= editor_auth(i18n->ed_cli_auth.c_str(), i18n, &set->auth);

    CHECKBOX_FLAG(set->flags, changed, CLIENT_PROXY, i18n->ed_cli_proxy.c_str());
    if (set->flags & CLIENT_PROXY) {
        changed |= ImGui::InputText(i18n->ed_cli_proxy_host.c_str(), &set->proxy_host);
        changed |= ImGui::InputInt(i18n->ed_cli_proxy_port.c_str(), &set->proxy_port);
        changed |= editor_auth(i18n->ed_cli_proxy_auth.c_str(), i18n, &set->proxy_auth);
    }

    size_t step = 1;
    changed |= ImGui::InputScalar(i18n->ed_cli_reruns.c_str(), ImGuiDataType_U64, &set->test_reruns,
                                  &step);

    changed |= ImGui::InputScalar(i18n->ed_cli_timeout.c_str(), ImGuiDataType_U64,
                                  &set->seconds_timeout, &step);

    return changed;
}

void show_httplib_headers(AppState* app, const httplib::Headers& headers) noexcept {
    if (ImGui::BeginTable("headers", 2, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        ImGui::PushFont(app->mono_font);
        for (const auto& [key, value] : headers) {
            ImGui::TableNextRow();
            ImGui::PushID((key + value).c_str());
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                // is given readonly flag so const_cast is fine
                ImGui::InputText("##key", &const_cast<std::string&>(key),
                                 ImGuiInputTextFlags_ReadOnly);
            }
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                // is given readonly flag so const_cast is fine
                ImGui::InputText("##value", &const_cast<std::string&>(value),
                                 ImGuiInputTextFlags_ReadOnly);
            }
            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

void show_httplib_cookies(AppState* app, const httplib::Headers& headers) noexcept {
    if (ImGui::BeginTable("cookies", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn(" ");
        ImGui::TableSetupColumn("Key");
        ImGui::TableSetupColumn("Value");
        ImGui::TableHeadersRow();
        ImGui::PushFont(app->mono_font);
        for (const auto& [key, value] : headers) {
            if (key != "Set-Cookie" && key != "Cookie") {
                continue;
            }

            ImGui::PushID(value.c_str());
            bool open = false;
            size_t search_idx = 0;
            while (search_idx != std::string::npos) {
                size_t key_val_split = value.find("=", search_idx);
                size_t next_pair = value.find(";", search_idx);
                std::string cookie_key, cookie_value;

                if (key_val_split == std::string::npos ||
                    (key_val_split > next_pair && next_pair != std::string::npos)) {
                    // Single key without value, example:
                    // A=b; Secure; HttpOnly; More=c
                    cookie_key = value.substr(search_idx, next_pair - search_idx);
                } else {
                    cookie_key = value.substr(search_idx, key_val_split - search_idx);
                    cookie_value = value.substr(key_val_split + 1, next_pair - (key_val_split + 1));
                }

                bool cookie_attribute = is_cookie_attribute(cookie_key);

                // Close when new cookie starts
                if (open && !cookie_attribute) {
                    open = false;
                    ImGui::TreePop();
                    ImGui::PopID();
                }
                if (open || !cookie_attribute) {
                    ImGui::TableNextRow();

                    ImGui::PushID(static_cast<int32_t>(search_idx));
                    if (ImGui::TableNextColumn() && !cookie_attribute) {
                        open = ImGui::TreeNodeEx("##tree_node", ImGuiTreeNodeFlags_SpanFullWidth);
                    }
                    if (ImGui::TableNextColumn()) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##key", &cookie_key, ImGuiInputTextFlags_ReadOnly);
                    }
                    if (ImGui::TableNextColumn()) {
                        ImGui::SetNextItemWidth(-1);
                        ImGui::InputText("##value", &cookie_value, ImGuiInputTextFlags_ReadOnly);
                    }

                    // Pop id only for cookie attributes and not open cookies
                    if (!open || cookie_attribute) {
                        ImGui::PopID();
                    }
                }

                if (next_pair == std::string::npos) {
                    break;
                }

                search_idx = next_pair + 1;                  // Skip over semicolon
                while (std::isspace(value.at(search_idx))) { // Skip over white spaces
                    search_idx += 1;
                }
            }

            // close last
            if (open) {
                open = false;
                ImGui::TreePop();
                ImGui::PopID();
            }

            ImGui::PopID();
        }
        ImGui::PopFont();
        ImGui::EndTable();
    }
}

ModalResult open_result_details(AppState* app, TestResult* tr) noexcept {
    if (!ImGui::IsPopupOpen("Test Result Details")) {
        ImGui::OpenPopup("Test Result Details");
    }

    ModalResult result = MODAL_NONE;
    bool open = true;
    if (ImGui::BeginPopupModal("Test Result Details", &open)) {
        if (ImGui::Button(ICON_FA_ARROW_RIGHT " Goto original test", ImVec2(150, 50))) {
            if (app->tests.contains(tr->original_test.id)) {
                app->editor_open_tab(tr->original_test.id);
                result = MODAL_CONTINUE;
            } else {
                Log(LogLevel::Error, "Original test is missing");
            }
        }

        ImGui::SameLine();

        ImGui::BeginDisabled(tr->running.load());

        if (ImGui::Button(ICON_FA_REDO " Rerun test", ImVec2(150, 50))) {
            if (app->tests.contains(tr->original_test.id)) {
                rerun_test(app, tr);
                result = MODAL_CONTINUE;
            } else {
                Log(LogLevel::Error, "Original test is missing");
            }
        }

        ImGui::EndDisabled();

        ImGui::Text("%s - %s", TestResultStatusLabels[tr->status.load()], tr->verdict.c_str());

        if (ImGui::BeginTabBar("test_details")) {
            if (ImGui::BeginTabItem("Response")) {
                if (tr->http_result && tr->http_result.value()) {
                    const auto& http_result = tr->http_result.value();

                    if (http_result.error() != httplib::Error::Success) {
                        ImGui::Text("Error: %s", to_string(http_result.error()).c_str());
                    } else {
                        if (ImGui::BeginTabBar("response_details")) {
                            if (ImGui::BeginTabItem("Body")) {
                                ImGui::Text("%d - %s", http_result->status,
                                            httplib::status_message(http_result->status));

                                ImGui::SameLine();
                                hint("Expected: %s", tr->original_test.response.status.c_str());

                                {
                                    // TODO: Add a diff like view (very very hard)
                                    ImGui::PushFont(app->mono_font);
                                    // Is given readonly flag so const_cast is fine
                                    ImGui::InputTextMultiline(
                                        "##response_body", &const_cast<std::string&>(tr->res_body),
                                        ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);

                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                        ResponseBodyType body_type =
                                            tr->original_test.response.body_type;
                                        std::string body_type_str =
                                            app->i18n.ed_rs_body_types[body_type];
                                        ImGui::SetTooltip("Expected: %s\n%s", body_type_str.c_str(),
                                                          tr->original_test.response.body.c_str());
                                    }
                                    ImGui::PopFont();
                                }

                                ImGui::EndTabItem();
                            }

                            if (ImGui::BeginTabItem("Cookies")) {
                                // TODO: Add expected cookies in split window (hard)
                                show_httplib_cookies(app, http_result->headers);
                                ImGui::EndTabItem();
                            }

                            if (ImGui::BeginTabItem("Headers")) {
                                // TODO: Add expected headers in split window (hard)
                                show_httplib_headers(app, http_result->headers);

                                ImGui::EndTabItem();
                            }
                            ImGui::EndTabBar();
                        }
                    }
                } else {
                    ImGui::Text("No response");
                }
                ImGui::EndTabItem();
            }

            if (ImGui::BeginTabItem("Request")) {
                const auto* headers = &tr->req_headers;

                ImGui::Text("Endpoint: ");
                ImGui::SameLine();
                ImGui::InputText("##request_endpoint", &const_cast<std::string&>(tr->req_endpoint),
                                 ImGuiInputTextFlags_ReadOnly);

                if (ImGui::BeginTabBar("request_details")) {
                    if (ImGui::BeginTabItem("Body")) {
                        {
                            ImGui::PushFont(app->mono_font);
                            // Is given readonly flag so const_cast is fine
                            ImGui::InputTextMultiline(
                                "##response_body", &const_cast<std::string&>(tr->req_body),
                                ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);

                            ImGui::PopFont();
                        }

                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Cookies")) {
                        show_httplib_cookies(app, *headers);
                        ImGui::EndTabItem();
                    }

                    if (ImGui::BeginTabItem("Headers")) {
                        show_httplib_headers(app, *headers);

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
                ImGui::EndTabItem();
            }
            ImGui::EndTabBar();
        }

        ImGui::EndPopup();
    }

    if (!open) {
        result = MODAL_CONTINUE;
    }

    return result;
}

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab) noexcept {
    auto edit = &app->tests[tab.original_idx];

    assert(std::holds_alternative<Test>(*edit));
    auto& test = std::get<Test>(*edit);

    VariablesMap vars = app->get_test_variables(test.id);

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            (tab.name + "###" + to_string(tab.original_idx)).c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("test", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText(app->i18n.ed_endpoint.c_str(), &test.endpoint);

            // Don't display tooltip while endpoint is being edited
            if (!ImGui::IsItemActive() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", replace_variables(vars, test.endpoint).c_str());
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                changed = true;
                test_resolve_url_variables(app->get_test_variables(test.parent_id), &test);
            }

            if (ImGui::BeginCombo(app->i18n.ed_type.c_str(), HTTPTypeLabels[test.type])) {
                for (size_t i = 0; i < ARRAY_SIZE(HTTPTypeLabels); i++) {
                    if (ImGui::Selectable(HTTPTypeLabels[i], i == test.type)) {
                        changed = true;
                        test.type = static_cast<HTTPType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            hint(app->i18n.ed_variables_hint.c_str());
            ImGui::SameLine();
            if (ImGui::TreeNode(app->i18n.ed_variables.c_str())) {
                ImGui::PushFont(app->mono_font);
                changed = changed | partial_dict(app, &test.variables, "variables", vars);
                ImGui::PopFont();

                ImGui::TreePop();
            }

            changed |= editor_test_request(app, test);
            changed |= editor_test_response(app, test);

            ImGui::Text("%s", app->i18n.ed_cli_title.c_str());
            ImGui::Separator();

            ClientSettings cli_settings = app->get_cli_settings(test.id);

            bool parent_dynamic =
                !test.cli_settings.has_value() && cli_settings.flags & CLIENT_DYNAMIC;

            ImGui::BeginDisabled(parent_dynamic);

            bool override_settings = test.cli_settings.has_value();
            if (test.parent_id != -1ull &&
                ImGui::Checkbox(app->i18n.ed_cli_parent_override.c_str(), &override_settings)) {
                changed = true;

                if (override_settings) {
                    test.cli_settings = ClientSettings{};
                } else {
                    test.cli_settings = std::nullopt;
                }
            }

            ImGui::EndDisabled();

            if (parent_dynamic) {
                ImGui::SameLine();
                hint(app->i18n.ed_cli_parent_dynamic_hint.c_str());
            }

            ImGui::BeginDisabled(!test.cli_settings.has_value());

            if (editor_client_settings(&app->i18n, &cli_settings, false)) {
                changed = true;
                test.cli_settings = cli_settings;
            }

            ImGui::EndDisabled();
        }

        ImGui::EndChild();

        ImGui::EndTabItem();
    }

    if (!open) {
        result = TAB_CLOSED;
    }

    if (changed && result == TAB_NONE) {
        result = TAB_CHANGED;
    }

    return result;
}

EditorTabResult editor_tab_group(AppState* app, EditorTab& tab) noexcept {
    auto edit = &app->tests[tab.original_idx];

    assert(std::holds_alternative<Group>(*edit));
    auto& group = std::get<Group>(*edit);

    VariablesMap vars = app->get_test_variables(group.id);

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            (tab.name + "###" + to_string(tab.original_idx)).c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("group", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText(app->i18n.ed_name.c_str(), &group.name);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            tooltip("%s", replace_variables(app->get_test_variables(group.id), group.name).c_str());

            hint("%s", app->i18n.ed_variables_hint.c_str());
            ImGui::SameLine();
            if (ImGui::TreeNode(app->i18n.ed_variables.c_str())) {
                partial_dict(app, &group.variables, "variables", vars);
                ImGui::TreePop();
            }

            ImGui::Text("%s", app->i18n.ed_cli_title.c_str());
            ImGui::Separator();

            ClientSettings cli_settings = app->get_cli_settings(group.id);

            bool parent_dynamic =
                !group.cli_settings.has_value() && cli_settings.flags & CLIENT_DYNAMIC;

            ImGui::BeginDisabled(parent_dynamic);

            bool override_settings = group.cli_settings.has_value();
            if (group.parent_id != -1ull &&
                ImGui::Checkbox(app->i18n.ed_cli_parent_override.c_str(), &override_settings)) {
                changed = true;
                if (override_settings) {
                    group.cli_settings = ClientSettings{};
                } else {
                    group.cli_settings = std::nullopt;
                }
            }

            ImGui::EndDisabled();

            if (parent_dynamic) {
                ImGui::SameLine();
                hint(app->i18n.ed_cli_parent_dynamic_hint.c_str());
            }

            ImGui::BeginDisabled(!group.cli_settings.has_value());

            if (editor_client_settings(&app->i18n, &cli_settings, true)) {
                changed = true;
                group.cli_settings = cli_settings;

                if (cli_settings.flags & CLIENT_DYNAMIC) {
                    // Remove cli_settings from every child

                    size_t id = group.id;
                    size_t child_idx = 0;
                    while (id != group.parent_id && !group.children_ids.empty()) {
                        assert(app->tests.contains(id));
                        NestedTest* nt = &app->tests.at(id);

                        assert(std::holds_alternative<Group>(*nt));
                        Group* iterated_group = &std::get<Group>(*nt);

                        assert(child_idx < iterated_group->children_ids.size());
                        assert(app->tests.contains(iterated_group->children_ids.at(child_idx)));
                        std::visit(SetClientSettingsVisitor{std::nullopt},
                                   app->tests.at(iterated_group->children_ids.at(child_idx)));

                        iterate_over_nested_children(app, &id, &child_idx, group.parent_id);
                    }
                }
            }

            ImGui::EndDisabled();
        }

        ImGui::EndChild();

        ImGui::EndTabItem();
    }

    if (!open) {
        result = TAB_CLOSED;
    }

    if (changed && result == TAB_NONE) {
        result = TAB_CHANGED;
    }

    return result;
}

void tabbed_editor(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);

    static bool show_homepage = true;

    if (app->editor.open_tabs.size() <= 0) {
        show_homepage = true;
    }

    if (ImGui::BeginTabBar("editor", ImGuiTabBarFlags_Reorderable)) {
        if (ImGui::BeginTabItem(app->i18n.ed_home.c_str(),
                                app->editor.open_tabs.size() > 0 ? &show_homepage : nullptr)) {
            ImGui::Text("%s", app->i18n.ed_home_content.c_str());
            ImGui::EndTabItem();
        }

        size_t closed_id = -1ull;
        for (auto& [id, tab] : app->editor.open_tabs) {
            NestedTest* original = &app->tests[tab.original_idx];
            EditorTabResult result;
            switch (app->tests[tab.original_idx].index()) {
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
            case TAB_CLOSED:
                closed_id = id;
                break;
            case TAB_CHANGED:
                tab.name = std::visit(LabelVisitor(), *original);
                tab.just_opened = true; // to force refocus after
                app->undo_history.push_undo_history(app);
                break;
            case TAB_NONE:
                break;
            }
        }

        if (closed_id != -1ull) {
            app->editor.open_tabs.erase(closed_id);
        }
        ImGui::EndTabBar();
    }

    ImGui::PopFont();
}

void testing_result_row(AppState* app, size_t result_id,
                        std::vector<TestResult>& results) noexcept {
    auto deselect_all = [app]() {
        for (auto& [_, results] : app->test_results) {
            for (auto& result : results) {
                result.selected = false;
            }
        }
    };

    auto shift_multiselect = [app, result_id](size_t result_idx) {
        bool selection_start = false;

        for (auto& [sel_result_id, sel_results] : app->test_results) {
            for (size_t sel_result_idx = 0; sel_result_idx < sel_results.size(); sel_result_idx++) {
                TestResult& sel_result = sel_results.at(sel_result_idx);

                if (!(sel_result.status.load() == app->results.filter ||
                      (sel_result.status.load() > app->results.filter &&
                       app->results.filter_cumulative))) {
                    continue;
                }

                sel_result.selected = selection_start;

                if ((sel_result_id == app->results.last_selected_id &&
                     sel_result_idx == app->results.last_selected_idx) ||
                    (sel_result_id == result_id && sel_result_idx == result_idx)) {
                    selection_start = !selection_start;
                    sel_result.selected = true;
                }
            }
        }
    };

    ImGui::PushID(static_cast<int32_t>(result_id));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, {ImGui::GetStyle().FramePadding.x, 0});
    for (size_t result_idx = 0; result_idx < results.size(); result_idx++) {
        TestResult& result = results.at(result_idx);

        if (!(result.status.load() == app->results.filter ||
              (result.status.load() > app->results.filter && app->results.filter_cumulative))) {
            continue;
        }

        ImGui::TableNextRow();
        ImGui::PushID(static_cast<int32_t>(result_idx));
        // Test type and Name
        if (ImGui::TableNextColumn()) {
            http_type_button(result.original_test.type);
            ImGui::SameLine();

            if (ImGui::Selectable(result.original_test.endpoint.c_str(), result.selected,
                                  SELECTABLE_FLAGS, ImVec2(0, 0))) {
                if (ImGui::GetIO().MouseDoubleClicked[ImGuiMouseButton_Left]) {
                    result.open = true;
                }
                auto& io = ImGui::GetIO();
                if (io.KeyCtrl) {
                    result.selected = !result.selected;
                } else if (io.KeyShift) {
                    shift_multiselect(result_idx);
                } else {
                    deselect_all();
                    result.selected = true;
                }

                app->results.last_selected_id = result_id;
                app->results.last_selected_idx = result_idx;
            }

            if (ImGui::BeginPopupContextItem("##testing_result_context")) {
                if (!result.selected) {
                    deselect_all();
                    result.selected = true;
                }

                if (ImGui::MenuItem(ICON_FA_NEWSPAPER " Details"
                                                      "###details")) {
                    result.open = true;
                }

                if (ImGui::MenuItem(ICON_FA_ARROW_RIGHT " Goto original test"
                                                        "###goto_original")) {
                    if (app->tests.contains(result.original_test.id)) {
                        app->editor_open_tab(result.original_test.id);
                    } else {
                        Log(LogLevel::Error, "Original test is missing");
                    }
                }

                bool any_running = false;
                bool any_not_running = false;
                for (auto& [_, results] : app->test_results) {
                    for (auto& rt : results) {
                        if (rt.selected) {
                            any_running |= rt.running.load();
                            any_not_running |= !rt.running.load();
                        }
                    }
                }

                if (ImGui::MenuItem(ICON_FA_REDO " Rerun tests"
                                                 "###rerun_tests",
                                    nullptr, false, any_not_running)) {
                    for (auto& [_, results] : app->test_results) {
                        for (auto& rt : results) {
                            if (rt.selected && !rt.running.load()) {
                                rerun_test(app, &rt);
                            }
                        }
                    }
                }

                if (ImGui::MenuItem(ICON_FA_STOP " Stop tests"
                                                 "###stop_tests",
                                    nullptr, false, any_running)) {
                    for (auto& [_, results] : app->test_results) {
                        for (auto& rt : results) {
                            if (rt.selected && rt.running.load()) {
                                stop_test(&rt);
                            }
                        }
                    }
                }

                ImGui::EndPopup();
            }
        }

        // Status
        if (ImGui::TableNextColumn()) {
            ImGui::Text("%s", TestResultStatusLabels[result.status.load()]);
        }

        // Verdict
        if (ImGui::TableNextColumn()) {
            if (result.status.load() == STATUS_RUNNING) {
                ImGui::ProgressBar(result.progress_total == 0
                                       ? 0
                                       : static_cast<float>(result.progress_current) /
                                             result.progress_total);
            } else {
                ImGui::Text("%s", result.verdict.c_str());
            }
        }

        ImGui::PopID();

        if (result.open) {
            auto modal = open_result_details(app, &result);
            result.open &= modal == MODAL_NONE;
        }
    }

    ImGui::PopStyleVar();
    ImGui::PopID();
}

void testing_results(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);

    if (ImGui::BeginCombo(ICON_FA_FILTER " Filter", TestResultStatusLabels[app->results.filter])) {
        for (size_t i = 0; i < ARRAY_SIZE(TestResultStatusLabels); i++) {
            if (ImGui::Selectable(TestResultStatusLabels[i], i == app->results.filter)) {
                app->results.filter = static_cast<TestResultStatus>(i);
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::Checkbox("Cumulative", &app->results.filter_cumulative);

    if (ImGui::BeginTable("results", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Test");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Verdict");
        ImGui::TableHeadersRow();

        for (auto& [result_id, results] : app->test_results) {
            testing_result_row(app, result_id, results);
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
}

template <class Data>
bool show_requestable_wait(const Requestable<Data>& requestable, std::string message) {
    if (requestable.status == REQUESTABLE_WAIT) {
        ImSpinner::SpinnerIncDots("wait", 5, 1);
        ImGui::SameLine();
        ImGui::Text("%s", message.c_str());
    }
    return requestable.status != REQUESTABLE_WAIT;
}

template <class Data> bool show_requestable_error(const Requestable<Data>& requestable) {
    if (requestable.status == REQUESTABLE_ERROR) {
        ImGui::TextColored({1, 0, 0, 1}, "%s", requestable.error.c_str());
        return true;
    }

    return false;
}

void remote_auth(AppState* app) noexcept {
    if (show_requestable_wait(app->conf.sync_session,
                              "Please wait, your authentication request is being processed")) {
        ImGui::Text("Login/Register");
        ImGui::TextColored({1, 0, 0, 1}, "%s", app->conf.sync_session.error.c_str());

        ImGui::InputText("Name##name", &app->conf.sync_name);

        ImGui::InputText("Password##password", &app->conf.sync_password,
                         ImGuiInputTextFlags_Password);

        ImGui::Checkbox("Remember Me##remember", &app->sync.remember_me);

        if (ImGui::Button("Login")) {
            httplib::Params params = {
                {"name", app->conf.sync_name},
                {"password", app->conf.sync_password},
                {"remember_me", app->sync.remember_me ? "1" : "0"},
            };
            execute_requestable_async(app, app->conf.sync_session, HTTP_GET,
                                      app->conf.sync_hostname, "/login", "", params,
                                      [app](auto& sync_session, std::string data) {
                                          sync_session.data = data;
                                          app->conf.save_file();
                                      });
        }

        ImGui::SameLine();

        if (ImGui::Button("Register")) {
            httplib::Params params = {
                {"name", app->conf.sync_name},
                {"password", app->conf.sync_password},
            };
            execute_requestable_async(
                app, app->conf.sync_session, HTTP_GET, app->conf.sync_hostname, "/register", "",
                params,
                [](auto& requestable, const std::string& data) { requestable.data = data; });
        }
    }
}

void remote_file_editor(AppState* app) noexcept {
    if (app->sync.files.status == REQUESTABLE_NONE) {
        remote_file_list(app);
    }

    if (show_requestable_wait(app->sync.files, "Fetching a list of your files")) {
        if (ImGui::Button("Retry list fetch", {100, 24})) {
            remote_file_list(app);
        }
    }

    if (app->sync.files.status == REQUESTABLE_FOUND) {
        show_requestable_error(app->sync.files);
        show_requestable_error(app->sync.file_open);
        show_requestable_error(app->sync.file_save);
        show_requestable_error(app->sync.file_delete);
        show_requestable_error(app->sync.file_rename);

        if (show_requestable_wait(app->sync.file_open, "Opening file...")) {
            if (app->sync.file_open.status == REQUESTABLE_FOUND) {
                std::stringstream ss;
                ss << app->sync.file_open.data;

                app->open_file(ss);

                app->sync.file_open = {};
            }
        }

        if (ImGui::BeginChild("file-selection", ImVec2(0, 200), ImGuiChildFlags_FrameStyle)) {
            for (size_t i = 0; i < app->sync.files.data.size(); i++) {
                std::string& name = app->sync.files.data[i];

                ImGui::PushID(name.c_str());

                if (ImGui::Selectable((name + "##selectable").c_str(), false,
                                      ImGuiSelectableFlags_AllowDoubleClick)) {
                    remote_file_open(app, name);
                }

                if (ImGui::BeginPopupContextItem("##sync_filename_context")) {
                    if (ImGui::MenuItem("Open")) {
                        remote_file_open(app, name);
                    }
                    if (ImGui::MenuItem("Save")) {
                        remote_file_save(app, name);

                        app->saved_file = RemoteFile{name};
                    }
                    if (ImGui::MenuItem("Delete")) {
                        remote_file_delete(app, name);
                    }
                    if (ImGui::BeginMenu("Rename##rename-menu")) {
                        static std::string new_file_name;

                        ImGui::InputText("##new_file_name", &new_file_name);
                        ImGui::SameLine();
                        if (ImGui::Button("Rename##rename-confirm")) {
                            remote_file_rename(app, name, new_file_name);
                        }

                        ImGui::EndMenu();
                    }
                    ImGui::EndPopup();
                }

                ImGui::PopID();
            }
        }
        ImGui::EndChild();
    }

    if (show_requestable_wait(app->sync.file_save, "Saving file...")) {
        ImGui::Text("Save a file:");
        ImGui::InputText("##file_name", &app->sync.filename);

        ImGui::SameLine();

        if (ImGui::Button("Save")) {
            remote_file_save(app, app->sync.filename);

            app->saved_file = RemoteFile{app->sync.filename};
        }
    }
}

void remote_file_sync(AppState* app) noexcept {
    if (ImGui::Begin("Remote File Sync", &app->sync.show)) {
        ImGui::InputText("Remote Host##host", &app->conf.sync_hostname);

        if (ImGui::IsItemDeactivatedAfterEdit()) {
            app->conf.save_file();
        }

        if (app->conf.sync_session.status != REQUESTABLE_FOUND) {
            remote_auth(app);
        } else {
            ImGui::Text("You are logged in as %s", app->conf.sync_name.c_str());

            ImGui::SameLine();

            if (ImGui::Button("Logout", {100, 0})) {
                httplib::Params params = {
                    {"session_token", app->conf.sync_session.data},
                };
                execute_requestable_async(app, app->conf.sync_session, HTTP_GET,
                                          app->conf.sync_hostname, "/logout", "", params,
                                          [app](auto& requestable, std::string data) {
                                              app->conf.sync_session.status = REQUESTABLE_NONE;
                                          });
                app->conf.sync_session.data = "";
                app->sync = {.show = true};
            }

            remote_file_editor(app);
        }
    }

    ImGui::End();
}

void settings(AppState* app) noexcept {
    if (ImGui::Begin("Settings", &app->settings.show)) {
        ImGui::InputText("Search", &app->settings.search);

        if (ImGui::BeginChild("settings_scroll", {-1, -1})) {
            if (str_contains("Language" + app->i18n.settings_language, app->settings.search)) {
                if (ImGui::BeginCombo(
                        app->i18n.settings_language.c_str(),
                        app->i18n.settings_language_selection[app->conf.language].c_str())) {
                    for (const auto& [key, value] : app->i18n.settings_language_selection) {
                        if (ImGui::Selectable(value.c_str(), app->conf.language == key)) {
                            app->conf.language = key;

                            app->load_i18n(); // references to key and value are invalidated

                            app->conf.save_file();

                            break; // break to stop accessing key and value
                        }
                    }
                    ImGui::EndCombo();
                }
            }

            if (str_contains("Backups", app->settings.search)) {
                bool changed = false;

                ImGui::Separator();
                ImGui::Text("Backups");

                {
                    uint32_t time = app->conf.backup.time_to_backup;
                    static char buffer[32] = {0};
                    snprintf(buffer, 31, "%02d:%02d:%02d", time / 3600 % 24, time / 60 % 60,
                             time % 60);

                    uint32_t min = 60;
                    uint32_t max = 60 * 60;

                    changed |=
                        ImGui::SliderScalar("Time to backup", ImGuiDataType_U32,
                                            &app->conf.backup.time_to_backup, &min, &max, buffer);
                }

                changed |= ImGui::InputScalar("Local backups", ImGuiDataType_U8,
                                              &app->conf.backup.local_to_keep);
                changed |= ImGui::InputScalar("Remote backups", ImGuiDataType_U8,
                                              &app->conf.backup.remote_to_keep);

                static bool override = app->conf.backup.local_dir.has_value();

                ImGui::Text("Local backup directory");
                if (ImGui::Checkbox("##override_local_dir", &override)) {
                    changed = true;

                    if (override) {
                        app->conf.backup.local_dir = app->conf.backup.get_local_dir();
                    } else {
                        app->conf.backup.local_dir = std::nullopt;
                    }
                }

                ImGui::SameLine();

                static std::string default_local_dir = app->conf.backup.get_default_local_dir();

                ImGui::BeginDisabled(!override);
                changed |=
                    ImGui::InputText("##local_dir", override ? &app->conf.backup.local_dir.value()
                                                             : &default_local_dir);
                ImGui::EndDisabled();

                if (changed) {
                    app->conf.save_file();
                }
            }
        }
        ImGui::EndChild();
    }
    ImGui::End();
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
    auto log_split =
        HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.35f);
    auto tests_split =
        HelloImGui::DockingSplit("MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.35f);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState* app) noexcept {
    auto tab_editor_window = HelloImGui::DockableWindow("Editor###win_editor", "MainDockSpace",
                                                        [app]() { tabbed_editor(app); });

    auto tests_window = HelloImGui::DockableWindow("Tests###win_tests", "SideBarDockSpace",
                                                   [app]() { tree_view(app); });

    auto results_window = HelloImGui::DockableWindow("Results###win_results", "MainDockSpace",
                                                     [app]() { testing_results(app); });

    auto logs_window = HelloImGui::DockableWindow("Logs###win_logs", "LogDockSpace",
                                                  [app]() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, results_window, logs_window};
}

HelloImGui::DockingParams layout(AppState* app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
}

void save_as_file_dialog(AppState* app) noexcept {
    auto save_file_dialog =
        pfd::save_file("Save To", ".", {"Weetee Files", "*.wt", "All Files", "*"}, pfd::opt::none);

    std::string name = save_file_dialog.result();

    if (name.size() > 0) {
        app->saved_file = LocalFile{name};
        std::ofstream out(name);
        Log(LogLevel::Info, "Saving to local file '%s'", name.c_str());
        if (app->save_file(out)) {
            Log(LogLevel::Info, "Successfully saved to '%s'!", name.c_str());
        }
    }
}

void save_file_dialog(AppState* app) noexcept {
    switch (app->saved_file.index()) {
    case SAVED_FILE_NONE: {
        save_as_file_dialog(app);
    } break;
    case SAVED_FILE_REMOTE: {
        std::string name = std::get<RemoteFile>(app->saved_file).filename;
        Log(LogLevel::Info, "Saving to remote file '%s'", name.c_str());
        remote_file_save(app, name);
    } break;
    case SAVED_FILE_LOCAL: {
        std::string name = std::get<LocalFile>(app->saved_file).filename;
        std::ofstream out(name);
        Log(LogLevel::Info, "Saving to local file '%s'", name.c_str());
        if (app->save_file(out)) {
            Log(LogLevel::Info, "Successfully saved to '%s'!", name.c_str());
        }
    } break;
    }
}

void open_file_dialog(AppState* app) noexcept {
    auto open_file_dialog = pfd::open_file(
        "Open File", ".", {"Weetee Files", "*.wt", "All Files", "*"}, pfd::opt::none);

    std::vector<std::string> result = open_file_dialog.result();
    if (result.size() > 0) {
        app->saved_file = LocalFile{result[0]};
        std::ifstream in(result[0]);
        app->open_file(in);
    }
}

void import_swagger_file_dialog(AppState* app) noexcept {
    auto import_swagger_file_dialog =
        pfd::open_file("Import Swagger JSON File", ".", {"JSON", "*.json"}, pfd::opt::none);

    std::vector<std::string> result = import_swagger_file_dialog.result();
    if (result.size() > 0) {
        app->import_swagger(result[0]);
    }
}

void export_swagger_file_dialog(AppState* app) noexcept {
    auto export_swagger_file_dialog =
        pfd::save_file("Export To", ".", {"JSON", "*.json"}, pfd::opt::none);

    std::string result = export_swagger_file_dialog.result();
    if (result.size() > 0) {
        app->export_swagger(result);
    }
}

void show_menus(AppState* app) noexcept {
    if (ImGui::BeginMenu(app->i18n.menu_file.c_str())) {
        if (ImGui::MenuItem(app->i18n.menu_file_save_as.c_str(), "Ctrl + Shift + S")) {
            save_as_file_dialog(app);
        }
        if (ImGui::MenuItem(app->i18n.menu_file_save.c_str(), "Ctrl + S")) {
            save_file_dialog(app);
        }
        if (ImGui::MenuItem(app->i18n.menu_file_open.c_str(), "Ctrl + O")) {
            open_file_dialog(app);
        }
        if (ImGui::BeginMenu(app->i18n.menu_file_import.c_str())) {
            if (ImGui::MenuItem("Swagger JSON")) {
                import_swagger_file_dialog(app);
            }
            ImGui::EndMenu();
        }
        if (ImGui::BeginMenu(app->i18n.menu_file_export.c_str())) {
            if (ImGui::MenuItem("Swagger JSON")) {
                export_swagger_file_dialog(app);
            }
            ImGui::EndMenu();
        }

        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu(app->i18n.menu_edit.c_str())) {
        if (ImGui::MenuItem(app->i18n.menu_edit_undo.c_str(), "Ctrl + Z", nullptr,
                            app->undo_history.can_undo())) {
            app->undo();
        } else if (ImGui::MenuItem(app->i18n.menu_edit_redo.c_str(), "Ctrl + Shift + Z", nullptr,
                                   app->undo_history.can_redo())) {
            app->redo();
        }
        ImGui::EndMenu();
    }

    if (ImGui::MenuItem("Remote File Sync")) {
        app->sync.show = true;
    }

    if (ImGui::MenuItem("Settings")) {
        app->settings.show = true;
    }

    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (!app->is_running_tests() && arrow("start", ImGuiDir_Right)) {
        std::vector<size_t> tests_to_run =
            get_tests_to_run(app, MapKeyIterator<size_t, NestedTest>(app->tests.begin()),
                             MapKeyIterator<size_t, NestedTest>(app->tests.end()));

        run_tests(app, tests_to_run);
    }
    ImGui::PopStyleColor(1);

    begin_transparent_button();
    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_DELETE]);
    if (app->is_running_tests() && ImGui::Button(ICON_FA_STOP)) {
        stop_tests(app);
        Log(LogLevel::Info, "Stopped testing");
    }
    ImGui::PopStyleColor(1);
    end_transparent_button();
}

void show_app_menu_items(AppState* app) noexcept {}

void show_gui(AppState* app) noexcept {
    auto io = ImGui::GetIO();
#ifndef NDEBUG
    // ImGui::ShowDemoWindow();
    ImGuiTestEngine* engine = HelloImGui::GetImGuiTestEngine();
    ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
#endif

    static bool first_call = true;
    if (first_call) {
        ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
        first_call = false;
    }

    if (!app->tree_view.window_focused) {
        app->tree_view.selected_tests.clear();
    }

    if (app->sync.show) {
        remote_file_sync(app);
    }

    if (app->settings.show) {
        settings(app);
    }

    bool changed = false;

    // SHORTCUTS
    //
    // Saving
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_as_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        open_file_dialog(app);
    }

    // Undo
    if (app->undo_history.can_undo() && io.KeyCtrl && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->undo();
    } else if (app->undo_history.can_redo() && io.KeyCtrl && io.KeyShift &&
               ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->redo();
    }

    // Tree view
    if (app->tree_view.selected_tests.size() > 0) {
        // Copy pasting
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            app->copy();
        } else if (!app->tree_view.selected_tests.contains(0) && io.KeyCtrl &&
                   ImGui::IsKeyPressed(ImGuiKey_X)) {
            changed = true;

            app->cut();
        } else if (app->can_paste() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            changed = true;

            auto top_layer = app->select_top_layer();

            if (top_layer.size() == 1) {
                NestedTest* nested_test = &app->tests[top_layer[0]];

                if (std::holds_alternative<Group>(*nested_test)) {
                    Group& selected_group = std::get<Group>(*nested_test);

                    app->paste(&selected_group);
                    app->select_with_children(selected_group.id);
                } else {
                    assert(app->tests.contains(std::visit(ParentIDVisitor(), *nested_test)));
                    assert(std::holds_alternative<Group>(
                        app->tests.at(std::visit(ParentIDVisitor(), *nested_test))));
                    Group& parent_group =
                        std::get<Group>(app->tests.at(std::visit(ParentIDVisitor(), *nested_test)));

                    app->paste(&parent_group);
                }
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            auto top_layer = app->select_top_layer();
            if (top_layer.size() == 1) {
                app->editor_open_tab(top_layer[0]);
            }
        }

        if (ImGui::IsKeyPressed(ImGuiKey_Delete)) {
            changed = true;
            if (!app->tree_view.selected_tests.contains(0)) { // root not selected
                app->delete_selected();
            }
        }
    }

    if (changed) {
        app->undo_history.push_undo_history(app);
    }
}

void pre_frame(AppState* app) noexcept {
    app->backup.time_since_last_backup += ImGui::GetIO().DeltaTime;

    if (app->backup.time_since_last_backup > app->conf.backup.time_to_backup) {
        app->backup.time_since_last_backup = 0;

        make_backups(app);
    }
}

void load_fonts(AppState* app) noexcept {
    app->regular_font =
        HelloImGui::LoadFont("fonts/DroidSans.ttf", 15, {.useFullGlyphRange = true});
    app->regular_font = HelloImGui::MergeFontAwesomeToLastFont(15);
    app->mono_font =
        HelloImGui::LoadFont("fonts/MesloLGS NF Regular.ttf", 15, {.useFullGlyphRange = true});
    app->awesome_font =
        HelloImGui::LoadFont("fonts/fontawesome-webfont.ttf", 15, {.useFullGlyphRange = true});
}

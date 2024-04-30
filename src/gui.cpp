#include "gui.hpp"

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "imspinner/imspinner.h"
#include "portable_file_dialogs/portable_file_dialogs.h"

#include "httplib.h"

#include "app_state.hpp"
#include "http.hpp"
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

template <class... Args> void tooltip(const char* format, Args... args) noexcept {
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
        ImGui::SetTooltip(format, args...);
    }
}

template <class... Args> bool hint(const char* format, Args... args) noexcept {
    bool result = ImGui::Button("?");
    tooltip(format, args...);
    return result;
}

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

            if (ImGui::MenuItem("Sort", nullptr, false,
                                analysis.top_selected_count == 1 && !changed)) {
                changed = true;

                NestedTest* nested_test = &app->tests.at(nested_test_id);
                assert(std::holds_alternative<Group>(*nested_test));
                auto& selected_group = std::get<Group>(*nested_test);
                selected_group.flags |= GROUP_OPEN;
                app->sort(selected_group);
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

            assert(app->tests.contains(test.parent_id));
            app->move(&std::get<Group>(app->tests.at(test.parent_id)));
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

bool partial_dict_data_row(AppState* app, Cookies*, CookiesElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(app->variables(), elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Parameters*, ParametersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(app->variables(), elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Headers*, HeadersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
        tooltip("%s", replace_variables(app->variables(), elem->data.data).c_str());
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, Variables*, VariablesElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState* app, MultiPartBody*, MultiPartBodyElement* elem) noexcept {
    bool changed = false;

    if (ImGui::TableNextColumn()) { // type
        ImGui::SetNextItemWidth(-1);
        if (ImGui::BeginCombo("##type", MPBDTypeLabels[elem->data.type])) {
            for (size_t i = 0; i < ARRAY_SIZE(MPBDTypeLabels); i++) {
                if (ImGui::Selectable(MPBDTypeLabels[i], i == elem->data.type)) {
                    elem->data.type = static_cast<MultiPartBodyDataType>(i);

                    switch (elem->data.type) {
                    case MPBD_TEXT:
                        elem->data.data = "";
                        elem->data.content_type = "text/plain";
                        if (elem->data.open_file.has_value()) {
                            elem->data.open_file->kill();
                            elem->data.open_file.reset();
                        }
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
            ImGui::SetNextItemWidth(-1);
            assert(std::holds_alternative<std::string>(elem->data.data));
            auto str = &std::get<std::string>(elem->data.data);

            if (ImGui::InputText("##text", str)) {
                changed = true;

                elem->data.resolve_content_type();
            }

            tooltip("%s", replace_variables(app->variables(), *str).c_str());
        } break;
        case MPBD_FILES:
            assert(std::holds_alternative<std::vector<std::string>>(elem->data.data));
            auto& files = std::get<std::vector<std::string>>(elem->data.data);
            std::string text = files.empty() ? "Select Files"
                                             : "Selected " + to_string(files.size()) +
                                                   " Files (Hover to see names)";
            if (ImGui::Button(text.c_str(), ImVec2(-1, 0))) {
                elem->data.open_file =
                    pfd::open_file("Select Files", ".", {"All Files", "*"}, pfd::opt::multiselect);
            }

            if (!files.empty() && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                std::stringstream ss;
                for (auto& file : files) {
                    ss << file << '\n';
                }
                ImGui::SetTooltip("%s", ss.str().c_str());
            }

            if (elem->data.open_file.has_value() && elem->data.open_file->ready()) {
                changed = true;

                auto result_files = elem->data.open_file->result();
                elem->data.data = result_files;
                elem->data.resolve_content_type();
                elem->data.open_file = std::nullopt;
            }
            break;
        }
    }

    if (ImGui::TableNextColumn()) { // content-type
        ImGui::SetNextItemWidth(-1);
        changed |= ImGui::InputText("##content_type", &elem->data.content_type);
        tooltip("%s", replace_variables(app->variables(), elem->data.content_type).c_str());
    }
    return changed;
}

bool editor_test_request(AppState* app, EditorTab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Request")) {
        ImGui::PushID("request");

        if (ImGui::BeginTabItem("Request")) {
            ImGui::Text("Select any of the tabs to edit test's request");
            ImGui::EndTabItem();
        }

        if (test.type != HTTP_GET && ImGui::BeginTabItem("Body")) {
            bool body_type_changed = false;
            if (ImGui::BeginCombo("Body Type", RequestBodyTypeLabels[test.request.body_type])) {
                for (size_t i = 0; i < ARRAY_SIZE(RequestBodyTypeLabels); i++) {
                    if (ImGui::Selectable(RequestBodyTypeLabels[i], i == test.request.body_type)) {
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
            case REQUEST_OTHER:
                {
                ImGui::PushFont(app->mono_font);
                std::string* body = &std::get<std::string>(test.request.body);
                changed |= ImGui::InputTextMultiline("##body", body, ImVec2(0, 300));
                tooltip("%s", replace_variables(app->variables(), *body).c_str());

                // TODO: This crashes when opened with response partial_dicts at the same time
                // if (test.request.body_type == REQUEST_JSON && ImGui::BeginPopupContextItem()) {
                //     if (ImGui::MenuItem("Format")) {
                //         assert(std::holds_alternative<std::string>(test.request.body));
                //         const char* error =
                //         json_format(&std::get<std::string>(test.request.body)); if (error) {
                //             Log(LogLevel::Error, "Failed to parse json: ", error);
                //         }
                //     }
                //     ImGui::EndPopup();
                // }

                ImGui::PopFont();
            } break;

            case REQUEST_MULTIPART:
                auto& mpb = std::get<MultiPartBody>(test.request.body);
                changed = changed | partial_dict(app, &mpb, "##body");
                break;
            }
            ImGui::EndTabItem();
        }

        if (!test.request.url_parameters.empty() && ImGui::BeginTabItem("URL Parameters")) {
            ImGui::PushFont(app->mono_font);
            static constexpr int32_t flags =
                PARTIAL_DICT_NO_DELETE | PARTIAL_DICT_NO_ENABLE | PARTIAL_DICT_NO_CREATE;
            changed = changed |
                      partial_dict(app, &test.request.url_parameters, "##url_parameters", flags);
            ImGui::PopFont();
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Parameters")) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.request.parameters, "##parameters");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Cookies")) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.request.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::PushFont(app->mono_font);
            changed =
                changed | partial_dict(app, &test.request.headers, "##headers", PARTIAL_DICT_NONE,
                                       RequestHeadersLabels, ARRAY_SIZE(RequestHeadersLabels));
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
        ImGui::PopID();
    }

    return changed;
}

bool editor_test_response(AppState* app, EditorTab, Test& test) noexcept {
    bool changed = false;

    if (ImGui::BeginTabBar("Response")) {
        ImGui::PushID("response");

        if (ImGui::BeginTabItem("Response")) {
            static ComboFilterState s{};
            ComboFilter("Status", &test.response.status, HTTPStatusLabels,
                        ARRAY_SIZE(HTTPStatusLabels), &s);
            ImGui::Text("Select any of the tabs to edit test's expected response");
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Body")) {
            bool body_type_changed = false;
            if (ImGui::BeginCombo("Body Type", ResponseBodyTypeLabels[test.response.body_type])) {
                for (size_t i = 0; i < ARRAY_SIZE(ResponseBodyTypeLabels); i++) {
                    if (ImGui::Selectable(ResponseBodyTypeLabels[i],
                                          i == test.response.body_type)) {
                        body_type_changed = true;
                        test.response.body_type = static_cast<ResponseBodyType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            if (body_type_changed) {
                changed = true;

                switch (test.response.body_type) {
                case RESPONSE_ANY:
                case RESPONSE_JSON:
                case RESPONSE_HTML:
                case RESPONSE_PLAIN:
                case RESPONSE_OTHER:
                    if (!std::holds_alternative<std::string>(test.response.body)) {
                        test.response.body = "";
                    }
                    break;
                }
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

                std::string* body = &std::get<std::string>(test.response.body);
                changed |= ImGui::InputTextMultiline("##body", body, ImVec2(0, 300));
                tooltip("%s", replace_variables(app->variables(), *body).c_str());

                // TODO: This crashes when opened with request partial_dicts at the same time
                // if (test.response.body_type == RESPONSE_JSON && ImGui::BeginPopupContextItem()) {
                //     if (ImGui::MenuItem("Format")) {
                //         assert(std::holds_alternative<std::string>(test.response.body));
                //         const char* error =
                //         json_format(&std::get<std::string>(test.response.body)); if (error) {
                //             Log(LogLevel::Error, "Failed to parse json: ", error);
                //         }
                //     }
                //     ImGui::EndPopup();
                // }

                ImGui::PopFont();

                break;
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Set Cookies")) {
            ImGui::PushFont(app->mono_font);
            changed = changed | partial_dict(app, &test.response.cookies, "##cookies");
            ImGui::PopFont();
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Headers")) {
            ImGui::PushFont(app->mono_font);
            changed =
                changed | partial_dict(app, &test.response.headers, "##headers", PARTIAL_DICT_NONE,
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

        if (ImGui::Button("Continue")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CONTINUE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Save")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_SAVE;
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            ImGui::CloseCurrentPopup();
            result = MODAL_CANCEL;
        }
        ImGui::EndPopup();
    }

    return result;
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
            // is given readonly flag so const_cast is fine
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
                ImGui::InputText("##key", &const_cast<std::string&>(key),
                                 ImGuiInputTextFlags_ReadOnly);
            }
            // is given readonly flag so const_cast is fine
            if (ImGui::TableNextColumn()) {
                ImGui::SetNextItemWidth(-1);
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
            if (key != "Set-Cookie") {
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

ModalResult open_result_details(AppState* app, const TestResult* tr) noexcept {
    if (!ImGui::IsPopupOpen("Test Result Details")) {
        ImGui::OpenPopup("Test Result Details");
    }

    ModalResult result = MODAL_NONE;
    bool open = true;
    if (ImGui::BeginPopupModal("Test Result Details", &open)) {
        if (ImGui::Button("Goto original test", ImVec2(150, 50))) {
            if (app->tests.contains(tr->original_test.id)) {
                app->editor_open_tab(tr->original_test.id);
            } else {
                Log(LogLevel::Error, "Original test is missing");
            }
        }

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
                                    // TODO: add a diff like view (very very hard)
                                    ImGui::PushFont(app->mono_font);
                                    // Is given readonly flag so const_cast is fine
                                    ImGui::InputTextMultiline(
                                        "##response_body",
                                        &const_cast<std::string&>(http_result->body),
                                        ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);

                                    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                        std::string body = "*Multipart Data*";
                                        if (std::holds_alternative<std::string>(
                                                tr->original_test.response.body)) {
                                            body = std::get<std::string>(
                                                tr->original_test.response.body);
                                        }
                                        ImGui::SetTooltip(
                                            "Expected: %s\n%s",
                                            ResponseBodyTypeLabels[tr->original_test.response
                                                                       .body_type],
                                            body.c_str());
                                    }
                                    ImGui::PopFont();
                                }

                                ImGui::EndTabItem();
                            }
                            if (ImGui::BeginTabItem("Cookies")) {
                                // TODO: add expected cookies in split window (hard)
                                show_httplib_cookies(app, http_result->headers);
                                ImGui::EndTabItem();
                            }
                            if (ImGui::BeginTabItem("Headers")) {
                                // TODO: add expected headers in split window (hard)
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

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            tab.name.c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("test", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText("Endpoint", &test.endpoint);

            // Don't display tooltip while endpoint is being edited
            if (!ImGui::IsItemActive() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", request_endpoint(app->variables(), &test).c_str());
            }

            if (ImGui::IsItemDeactivatedAfterEdit()) {
                changed = true;
                std::vector<std::string> param_names = parse_url_params(test.endpoint);

                // add new params
                for (const std::string& name : param_names) {
                    auto param =
                        std::find_if(test.request.url_parameters.elements.begin(),
                                     test.request.url_parameters.elements.end(),
                                     [&name](const auto& elem) { return elem.key == name; });
                    if (param == test.request.url_parameters.elements.end()) {
                        test.request.url_parameters.elements.push_back({.key = name});
                    }
                }

                // remove old ones
                std::erase_if(test.request.url_parameters.elements, [&param_names](
                                                                        const auto& elem) {
                    auto param =
                        std::find_if(param_names.begin(), param_names.end(),
                                     [&elem](const std::string& name) { return elem.key == name; });
                    return param == param_names.end();
                });
            }

            if (ImGui::BeginCombo("Type", HTTPTypeLabels[test.type])) {
                for (size_t i = 0; i < ARRAY_SIZE(HTTPTypeLabels); i++) {
                    if (ImGui::Selectable(HTTPTypeLabels[i], i == test.type)) {
                        changed = true;
                        test.type = static_cast<HTTPType>(i);
                    }
                }
                ImGui::EndCombo();
            }

            changed = changed | editor_test_request(app, tab, test);
            changed = changed | editor_test_response(app, tab, test);

            ImGui::Text("Client Settings");
            ImGui::Separator();

            bool enable_settings = test.cli_settings.has_value() || test.parent_id == -1ull;
            if (test.parent_id != -1ull && ImGui::Checkbox("Override Parent", &enable_settings)) {
                changed = true;
                if (enable_settings) {
                    test.cli_settings = ClientSettings{};
                } else {
                    test.cli_settings = std::nullopt;
                }
            }

            if (!enable_settings) {
                ImGui::BeginDisabled();
            }

            ClientSettings cli_settings = app->get_cli_settings(test.id);
            if (show_client_settings(&cli_settings)) {
                changed = true;
                test.cli_settings = cli_settings;
            }

            if (!enable_settings) {
                ImGui::EndDisabled();
            }

            ImGui::EndChild();
        }

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

    bool changed = false;

    EditorTabResult result = TAB_NONE;
    bool open = true;
    if (ImGui::BeginTabItem(
            tab.name.c_str(), &open,
            (tab.just_opened ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None))) {

        if (ImGui::BeginChild("group", ImVec2(0, 0), ImGuiChildFlags_None)) {
            ImGui::InputText("Name", &group.name);
            changed |= ImGui::IsItemDeactivatedAfterEdit();
            tooltip("%s", replace_variables(app->variables(), group.name).c_str());

            if (group.variables.has_value()) {
                ImGui::Text("Variables");
                ImGui::SameLine();
                hint("To use a variable, write it's name anywhere incapsulated in <>\nexample: "
                     "<host>/api/test/");
                partial_dict(app, &group.variables.value(), "variables");
            }

            ImGui::Text("Client Settings");
            ImGui::Separator();

            bool enable_settings = group.cli_settings.has_value() || group.parent_id == -1ull;
            if (group.parent_id != -1ull && ImGui::Checkbox("Override Parent", &enable_settings)) {
                changed = true;
                if (enable_settings) {
                    group.cli_settings = ClientSettings{};
                } else {
                    group.cli_settings = std::nullopt;
                }
            }

            if (!enable_settings) {
                ImGui::BeginDisabled();
            }

            ClientSettings cli_settings = app->get_cli_settings(group.id);
            if (show_client_settings(&cli_settings)) {
                changed = true;
                group.cli_settings = cli_settings;
            }

            if (!enable_settings) {
                ImGui::EndDisabled();
            }

            ImGui::EndChild();
        }

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

    if (ImGui::BeginTabBar("editor")) {
        size_t closed_id = -1ull;
        for (auto& [id, tab] : app->opened_editor_tabs) {
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
            case TAB_NONE:
                break;
            }
        }

        if (closed_id != -1ull) {
            app->opened_editor_tabs.erase(closed_id);
        }
        ImGui::EndTabBar();
    }
    ImGui::PopFont();
}

void testing_results(AppState* app) noexcept {
    ImGui::PushFont(app->regular_font);

    auto deselect_all = [app]() {
        for (auto& [_, rt] : app->test_results) {
            rt.selected = false;
        }
    };

    if (ImGui::BeginTable("results", 3, TABLE_FLAGS)) {
        ImGui::TableSetupColumn("Test");
        ImGui::TableSetupColumn("Status");
        ImGui::TableSetupColumn("Verdict");
        ImGui::TableHeadersRow();

        for (auto& [id, result] : app->test_results) {
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(result.original_test.id));

            // test type and name
            if (ImGui::TableNextColumn()) {
                http_type_button(result.original_test.type);
                ImGui::SameLine();

                if (ImGui::Selectable(result.original_test.endpoint.c_str(), result.selected,
                                      SELECTABLE_FLAGS, ImVec2(0, 0))) {
                    if (ImGui::GetIO().MouseDoubleClicked[ImGuiMouseButton_Left]) {
                        result.open = true;
                    }
                    if (ImGui::GetIO().KeyCtrl) {
                        result.selected = !result.selected;
                    } else {
                        deselect_all();
                        result.selected = true;
                    }
                }

                if (ImGui::BeginPopupContextItem()) {
                    if (!result.selected) {
                        deselect_all();
                        result.selected = true;
                    }

                    if (ImGui::MenuItem("Details")) {
                        result.open = true;
                    }

                    if (ImGui::MenuItem("Goto original test")) {
                        if (app->tests.contains(result.original_test.id)) {
                            app->editor_open_tab(result.original_test.id);
                        } else {
                            Log(LogLevel::Error, "Original test is missing");
                        }
                    }

                    if (ImGui::MenuItem("Stop")) {
                        app->stop_tests();
                    }

                    ImGui::EndPopup();
                }
            }

            // status
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", TestResultStatusLabels[result.status.load()]);
            }

            // verdict
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", result.verdict.c_str());
            }

            ImGui::PopID();

            if (result.open) {
                auto modal = open_result_details(app, &result);
                result.open &= modal == MODAL_NONE;
            }
        }

        ImGui::EndTable();
    }

    ImGui::PopFont();
}

std::vector<HelloImGui::DockingSplit> splits() noexcept {
    auto log_split = HelloImGui::DockingSplit("MainDockSpace", "LogDockSpace", ImGuiDir_Down, 0.2f);
    auto tests_split =
        HelloImGui::DockingSplit("MainDockSpace", "SideBarDockSpace", ImGuiDir_Left, 0.2f);
    return {log_split, tests_split};
}

std::vector<HelloImGui::DockableWindow> windows(AppState* app) noexcept {
    auto tab_editor_window =
        HelloImGui::DockableWindow("Editor", "MainDockSpace", [app]() { tabbed_editor(app); });

    auto tests_window =
        HelloImGui::DockableWindow("Tests", "SideBarDockSpace", [app]() { tree_view(app); });

    auto results_window =
        HelloImGui::DockableWindow("Results", "MainDockSpace", [app]() { testing_results(app); });

    auto logs_window =
        HelloImGui::DockableWindow("Logs", "LogDockSpace", [app]() { HelloImGui::LogGui(); });

    return {tests_window, tab_editor_window, results_window, logs_window};
}

HelloImGui::DockingParams layout(AppState* app) noexcept {
    auto params = HelloImGui::DockingParams();

    params.dockableWindows = windows(app);
    params.dockingSplits = splits();

    return params;
}

void save_as_file_dialog(AppState* app) noexcept {
    app->save_file_dialog = pfd::save_file("Save To", ".", {"All Files", "*"}, pfd::opt::none);
}

void save_file_dialog(AppState* app) noexcept {
    if (!app->filename.has_value()) {
        save_as_file_dialog(app);
    } else {
        app->save_file();
    }
}

void open_file_dialog(AppState* app) noexcept {
    app->open_file_dialog = pfd::open_file("Open File", ".", {"All Files", "*"}, pfd::opt::none);
}

void open_swagger_file_dialog(AppState* app) noexcept {
    app->open_swagger_file_dialog = pfd::open_file("Open Swagger JSON File", ".", {"All Files", "*.json"}, pfd::opt::none);
}

void show_menus(AppState* app) noexcept {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save As", "Ctrl + Shift + S")) {
            save_as_file_dialog(app);
        } else if (ImGui::MenuItem("Save", "Ctrl + S")) {
            save_file_dialog(app);
        } else if (ImGui::MenuItem("Open", "Ctrl + O")) {
            open_file_dialog(app);
        } else if (ImGui::MenuItem("Import Swagger JSON")) {
            open_swagger_file_dialog(app);
        }
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        if (ImGui::MenuItem("Undo", "Ctrl + Z", nullptr, app->undo_history.can_undo())) {
            app->undo();
        } else if (ImGui::MenuItem("Redo", "Ctrl + Shift + Z", nullptr,
                                   app->undo_history.can_redo())) {
            app->redo();
        }
        ImGui::EndMenu();
    }

    ImGui::PushStyleColor(ImGuiCol_Text, HTTPTypeColor[HTTP_GET]);
    if (!app->is_running_tests() && arrow("start", ImGuiDir_Right)) {
        // find tests to execute
        std::vector<Test> tests_to_run;
        for (const auto& [id, nested_test] : app->tests) {
            switch (nested_test.index()) {
            case TEST_VARIANT: {
                assert(std::holds_alternative<Test>(nested_test));
                const auto& test = std::get<Test>(nested_test);

                if (!(test.flags & TEST_DISABLED) && !app->parent_disabled(id)) {
                    tests_to_run.push_back(test);
                }
            } break;
            case GROUP_VARIANT:
                // ignore groups
                break;
            }
        }

        Log(LogLevel::Info, "Started testing for %d tests", tests_to_run.size());
        run_tests(app, &tests_to_run);
    }
    ImGui::PopStyleColor(1);

    if (app->is_running_tests() && ImGui::Button("Stop")) {
        app->stop_tests();
        Log(LogLevel::Warning, "Stopped testing");
    }
}

void show_gui(AppState* app) noexcept {
    auto io = ImGui::GetIO();
#ifndef NDEBUG
    ImGui::ShowDemoWindow();
    ImGuiTestEngine* engine = HelloImGui::GetImGuiTestEngine();
    ImGuiTestEngine_ShowTestEngineWindows(engine, nullptr);
#endif
    ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);

    if (!app->tree_view_focused) {
        app->selected_tests.clear();
    }

    // Opening
    if (app->open_file_dialog.has_value() && app->open_file_dialog->ready()) {
        auto result = app->open_file_dialog->result();

        if (result.size() > 0) {
            app->filename = result[0];
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->open_file();
        }

        app->open_file_dialog = std::nullopt;
    }

    // Saving
    if (app->save_file_dialog.has_value() && app->save_file_dialog->ready()) {
        if (app->save_file_dialog->result().size() > 0) {
            app->filename = app->save_file_dialog->result();
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->save_file();
        }

        app->save_file_dialog = std::nullopt;
    }

    // Importing
    if (app->open_swagger_file_dialog.has_value() && app->open_swagger_file_dialog->ready()) {
        auto result = app->open_swagger_file_dialog->result();

        if (result.size() > 0) {
            Log(LogLevel::Debug, "filename: %s", result[0].c_str());
            app->import_swagger(result[0]);
        }

        app->open_swagger_file_dialog = std::nullopt;
    }


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
    if (app->selected_tests.size() > 0) {
        // Copy pasting
        if (io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C)) {
            app->copy();
        } else if (!app->selected_tests.contains(0) && io.KeyCtrl &&
                   ImGui::IsKeyPressed(ImGuiKey_X)) {
            app->cut();
        } else if (app->can_paste() && io.KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V)) {
            auto top_layer = app->select_top_layer();
            if (top_layer.size() == 1) {
                NestedTest* parent = &app->tests[top_layer[0]];

                if (std::holds_alternative<Group>(*parent)) {
                    app->paste(&std::get<Group>(*parent));
                    app->undo_history.push_undo_history(app);
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
            if (!app->selected_tests.contains(0)) { // root not selected
                app->delete_selected();
            }
        }
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

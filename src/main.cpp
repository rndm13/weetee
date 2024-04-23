#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_font.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/imgui_theme.h"
#include "hello_imgui/internal/hello_imgui_ini_settings.h"
#include "hello_imgui/runner_params.h"

#include "imgui.h"
#include "imgui_stdlib.h"

#include "imgui_test_engine/imgui_te_context.h"
#include "immapp/immapp.h"

#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

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

// TODO: Swagger file import/export
// TODO: Implement file sending
// TODO: Implement variables for groups with substitution
// TODO: add fuzz tests
// TODO: Fix can_load...
// TODO: make dynamic tests work (and with keep alive connection)
// TODO: add authentication
// TODO: add move reordering

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

bool tree_view_show(AppState* app, NestedTest& nt, float indentation = 0) noexcept;
bool tree_view_show(AppState* app, Test& test, float indentation = 0) noexcept {
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

bool tree_view_show(AppState* app, Group& group, float indentation = 0) noexcept {
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
        changed = changed | tree_view_show(app, app->tests[0]);
        ImGui::EndTable();
    }

    if (changed) {
        app->filter(&app->tests[0]);
    }

    ImGui::PopFont();
}

template <typename Data>
bool partial_dict_row(AppState* app, PartialDict<Data>* pd, PartialDictElement<Data>* elem,
                      int32_t flags, const char** hints = nullptr,
                      const size_t hint_count = 0) noexcept {
    bool changed = false;
    auto select_only_this = [pd, elem]() {
        for (auto& e : pd->elements) {
            e.selected = false;
        }
        elem->selected = true;
    };
    if (ImGui::TableNextColumn()) {
        if (flags & PARTIAL_DICT_NO_ENABLE) {
            ImGui::BeginDisabled();
        }
        changed = changed | ImGui::Checkbox("##enabled", &elem->enabled);
        if (flags & PARTIAL_DICT_NO_ENABLE) {
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Selectable("##element", elem->selected, SELECTABLE_FLAGS, ImVec2(0, 0))) {
            if (ImGui::GetIO().KeyCtrl) {
                elem->selected = !elem->selected;
            } else {
                select_only_this();
            }
        }
        if (ImGui::BeginPopupContextItem()) {
            if (!elem->selected) {
                select_only_this();
            }

            if (!(flags & PARTIAL_DICT_NO_DELETE) && ImGui::MenuItem("Delete")) {
                changed = true;

                for (auto& e : pd->elements) {
                    e.to_delete = e.selected;
                }
            }

            if (ImGui::MenuItem("Enable")) {
                for (auto& e : pd->elements) {
                    e.enabled = e.enabled || e.selected;
                }
            }

            if (ImGui::MenuItem("Disable")) {
                for (auto& e : pd->elements) {
                    e.enabled = e.enabled && !e.selected;
                }
            }
            ImGui::EndPopup();
        }
    }
    if (ImGui::TableNextColumn()) { // name
        if (hint_count > 0) {
            assert(hints);
            if (!elem->cfs) {
                elem->cfs = ComboFilterState{};
            }
            ImGui::SetNextItemWidth(-1);
            changed =
                changed | ComboFilter("##name", &elem->key, hints, hint_count, &elem->cfs.value());
        } else {
            ImGui::SetNextItemWidth(-1);
            changed = changed | ImGui::InputText("##name", &elem->key);
        }
    }

    changed = changed | partial_dict_data_row(app, pd, elem);
    return changed;
}

bool partial_dict_data_row(AppState*, Cookies*, CookiesElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, Parameters*, ParametersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, Headers*, HeadersElement* elem) noexcept {
    bool changed = false;
    if (ImGui::TableNextColumn()) {
        changed = changed | ImGui::InputText("##data", &elem->data.data);
    }
    return changed;
}

bool partial_dict_data_row(AppState*, MultiPartBody*, MultiPartBodyElement* elem) noexcept {
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
                        if (elem->data.open_file.has_value()) {
                            elem->data.open_file->kill();
                            elem->data.open_file.reset();
                        }
                        break;
                    case MPBD_FILES:
                        elem->data.data = std::vector<std::string>{};
                        break;
                    }
                }
            }
            ImGui::EndCombo();
        }
    }
    if (ImGui::TableNextColumn()) { // body
        switch (elem->data.type) {
        case MPBD_TEXT:
            ImGui::SetNextItemWidth(-1);
            assert(std::holds_alternative<std::string>(elem->data.data));
            changed = changed | ImGui::InputText("##text", &std::get<std::string>(elem->data.data));
            break;
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
                // NOTE: could be slow to do this every frame
                std::stringstream ss;
                for (auto& file : files) {
                    ss << file << '\n';
                }
                ImGui::SetTooltip("%s", ss.str().c_str());
            }

            if (elem->data.open_file.has_value() && elem->data.open_file->ready()) {
                elem->data.data = elem->data.open_file->result();
                changed |= elem->data.open_file->result().size() > 0;
                elem->data.open_file = std::nullopt;
            }
            break;
        }
    }
    return changed;
}

template <typename Data>
bool partial_dict(AppState* app, PartialDict<Data>* pd, const char* label,
                  int32_t flags = PARTIAL_DICT_NONE, const char** hints = nullptr,
                  const size_t hint_count = 0) noexcept {
    using DataType = PartialDict<Data>::DataType;

    bool changed = false;

    //              name and checkbox        additional
    int32_t field_count = 2 + DataType::field_count;
    if (ImGui::BeginTable(label, field_count, TABLE_FLAGS, ImVec2(0, 300))) {
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, 17.0f);
        ImGui::TableSetupColumn("Name");
        for (size_t i = 0; i < DataType::field_count; i++) {
            ImGui::TableSetupColumn(DataType::field_labels[i]);
        }
        ImGui::TableHeadersRow();
        bool deletion = false;

        for (size_t i = 0; i < pd->elements.size(); i++) {
            auto* elem = &pd->elements[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(i));
            changed = partial_dict_row(app, pd, elem, flags, hints, hint_count);
            deletion |= elem->to_delete;
            ImGui::PopID();
        }

        if (deletion) {
            changed = true;

            pd->elements.erase(std::remove_if(pd->elements.begin(), pd->elements.end(),
                                              [](const auto& elem) { return elem.to_delete; }));
        }

        if (!(flags & PARTIAL_DICT_NO_CREATE)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); // enabled, skip
            if (ImGui::TableNextColumn()) {
                ImGui::Text("Change this to add new elements");
            }

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(pd->elements.size()));
            if (partial_dict_row(app, pd, &pd->add_element, flags, hints, hint_count)) {
                changed = true;

                pd->elements.push_back(pd->add_element);
                pd->add_element = {};
            }
            ImGui::PopID();
        }

        ImGui::EndTable();
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
                }
            }

            switch (test.request.body_type) {
            case REQUEST_JSON:
            case REQUEST_PLAIN:
                ImGui::PushFont(app->mono_font);
                changed = changed |
                          ImGui::InputTextMultiline(
                              "##body", &std::get<std::string>(test.request.body), ImVec2(0, 300));
                ImGui::PopFont();

                if (ImGui::BeginPopupContextItem()) {
                    if (test.request.body_type == REQUEST_JSON && ImGui::MenuItem("Format")) {
                        assert(std::holds_alternative<std::string>(test.request.body));
                        const char* error = json_format(&std::get<std::string>(test.request.body));
                        if (error) {
                            Log(LogLevel::Error, "Failed to parse json: ", error);
                        }
                    }
                    ImGui::EndPopup();
                }
                break;

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
                case RESPONSE_JSON:
                case RESPONSE_HTML:
                case RESPONSE_PLAIN:
                    if (!std::holds_alternative<std::string>(test.response.body)) {
                        test.response.body = "";
                    }
                    break;
                }
            }

            switch (test.response.body_type) {
            case RESPONSE_JSON:
            case RESPONSE_HTML:
            case RESPONSE_PLAIN:
                ImGui::PushFont(app->mono_font);
                changed = changed |
                          ImGui::InputTextMultiline(
                              "##body", &std::get<std::string>(test.response.body), ImVec2(0, 300));
                ImGui::PopFont();

                if (ImGui::BeginPopupContextItem()) {
                    if (test.response.body_type == RESPONSE_JSON && ImGui::MenuItem("Format")) {
                        assert(std::holds_alternative<std::string>(test.response.body));
                        const char* error = json_format(&std::get<std::string>(test.response.body));
                        if (error) {
                            Log(LogLevel::Error, "Failed to parse json: ", error);
                        }
                    }
                    ImGui::EndPopup();
                }

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

enum ModalResult : uint8_t {
    MODAL_NONE,
    MODAL_CONTINUE,
    MODAL_SAVE,
    MODAL_CANCEL,
};

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

constexpr bool is_cookie_attribute(std::string key) noexcept {
    std::for_each(key.begin(), key.end(), [](char& c) { c = static_cast<char>(std::tolower(c)); });
    return key == "domain" || key == "expires" || key == "httponly" || key == "max-age" ||
           key == "partitioned" || key == "path" || key == "samesite" || key == "secure";
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

        if (tr->http_result && tr->http_result.value()) {
            const auto& http_result = tr->http_result.value();

            if (http_result.error() != httplib::Error::Success) {
                ImGui::Text("Error: %s", to_string(http_result.error()).c_str());
            } else {
                if (ImGui::BeginTabBar("Response")) {
                    if (ImGui::BeginTabItem("Body")) {
                        ImGui::Text("%d - %s", http_result->status,
                                    httplib::status_message(http_result->status));

                        ImGui::SameLine();
                        ImGui::Button("?");
                        if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                            ImGui::SetTooltip("Expected: %s",
                                              tr->original_test.response.status.c_str());
                        }

                        {
                            // TODO: add a diff like view
                            ImGui::PushFont(app->mono_font);
                            // is given readonly flag so const_cast is fine
                            ImGui::InputTextMultiline(
                                "##response_body", &const_cast<std::string&>(http_result->body),
                                ImVec2(-1, 300), ImGuiInputTextFlags_ReadOnly);

                            if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                                std::string body = "*Multipart Data*";
                                if (std::holds_alternative<std::string>(
                                        tr->original_test.response.body)) {
                                    body = std::get<std::string>(tr->original_test.response.body);
                                }
                                ImGui::SetTooltip(
                                    "Expected: %s\n%s",
                                    ResponseBodyTypeLabels[tr->original_test.response.body_type],
                                    body.c_str());
                            }
                            ImGui::PopFont();
                        }

                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Cookies")) {
                        // TODO: add expected cookies in split window
                        show_httplib_cookies(app, http_result->headers);
                        ImGui::EndTabItem();
                    }
                    if (ImGui::BeginTabItem("Headers")) {
                        // TODO: add expected headers in split window
                        show_httplib_headers(app, http_result->headers);

                        ImGui::EndTabItem();
                    }
                    ImGui::EndTabBar();
                }
            }
        } else {
            ImGui::Text("No response");
        }

        ImGui::EndPopup();
    }

    if (!open) {
        result = MODAL_CONTINUE;
    }

    return result;
}

enum EditorTabResult : uint8_t {
    TAB_NONE,
    TAB_CLOSED,
    TAB_CHANGED,
};

std::vector<std::string> parse_url_params(const std::string& endpoint) noexcept {
    std::vector<std::string> result;
    size_t index = 0;
    do {
        size_t left_brace = endpoint.find("{", index);
        if (left_brace >= endpoint.size() - 1) { // Needs at least one character after
            break;
        }

        size_t right_brace = endpoint.find("}", left_brace);
        if (right_brace >= endpoint.size()) {
            break;
        }

        result.push_back(endpoint.substr(left_brace + 1, right_brace - (left_brace + 1)));

        index = right_brace + 1;
    } while (index < endpoint.size());

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
            if (!ImGui::IsItemActive() && !test.request.url_parameters.empty() &&
                ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) {
                ImGui::SetTooltip("%s", request_endpoint(&test).c_str());
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

std::pair<std::string, std::string> split_endpoint(std::string endpoint) {
    size_t semicolon = endpoint.find(":");
    if (semicolon == std::string::npos) {
        semicolon = 0;
    } else {
        semicolon += 3;
    }
    size_t slash = endpoint.find("/", semicolon);
    if (slash == std::string::npos) {
        return {endpoint, "/"};
    }

    std::string host = endpoint.substr(0, slash);
    std::string dest = endpoint.substr(slash);
    return {host, dest};
}

httplib::Headers request_headers(const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->request.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, header.data.data);
    }

    for (const auto& cookie : test->request.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Cookie", cookie.key + "=" + cookie.data.data);
    }

    return result;
}

httplib::Headers response_headers(const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->response.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, header.data.data);
    }

    for (const auto& cookie : test->response.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Set-Cookie", cookie.key + "=" + cookie.data.data);
    }

    return result;
}

struct ContentType {
    std::string type;
    std::string name;

    constexpr bool operator!=(const ContentType& other) noexcept {
        if (other.type != this->type) {
            return true;
        }
        return other.name == this->name;
    }
};
std::string to_string(const ContentType& cont) noexcept { return cont.type + '/' + cont.name; }
ContentType parse_content_type(std::string input) noexcept {
    size_t slash = input.find("/");
    size_t end = input.find(";");

    std::string type = input.substr(0, slash);
    std::string name = input.substr(slash + 1, end - (slash + 1));

    return {.type = type, .name = name};
}
ContentType response_content_type(ResponseBodyType type) noexcept {
    switch (type) {
    case RESPONSE_JSON:
        return {.type = "application", .name = "json"};
    case RESPONSE_HTML:
        return {.type = "text", .name = "html"};
    case RESPONSE_PLAIN:
        return {.type = "text", .name = "plain"};
    }
    assert(false && "Unreachable");
    return {};
}
ContentType request_content_type(RequestBodyType type) noexcept {
    switch (type) {
    case REQUEST_JSON:
        return {.type = "application", .name = "json"};
    case REQUEST_MULTIPART:
        return {.type = "multipart", .name = "form-data"};
    case REQUEST_PLAIN:
        return {.type = "text", .name = "plain"};
    }
    assert(false && "Unreachable");
    return {};
}

httplib::Params request_params(const Test* test) noexcept {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!param.enabled) {
            continue;
        }
        result.emplace(param.key, param.data.data);
    };

    return result;
}

httplib::Result make_request(AppState* app, const Test* test) noexcept {
    const auto params = request_params(test);
    const auto headers = request_headers(test);
    const std::string content_type = to_string(request_content_type(test->request.body_type));
    auto progress = [app, test](size_t current, size_t total) -> bool {
        // missing
        if (!app->test_results.contains(test->id)) {
            return false;
        }

        TestResult* result = &app->test_results.at(test->id);

        // stopped
        if (!result->running.load()) {
            result->status.store(STATUS_CANCELLED);

            return false;
        }

        result->progress_total = total;
        result->progress_current = current;
        result->verdict =
            to_string(static_cast<float>(current * 100) / static_cast<float>(total)) + "% ";

        return true;
    };

    httplib::Result result;
    std::string endpoint =
        request_endpoint(test) + "?" + httplib::detail::params_to_query_str(params);
    auto [host, dest] = split_endpoint(endpoint);
    httplib::Client cli(host);

    cli.set_compress(test->cli_settings->flags & CLIENT_COMPRESSION);
    cli.set_follow_location(test->cli_settings->flags & CLIENT_FOLLOW_REDIRECTS);

    switch (test->type) {
    case HTTP_GET:
        result = cli.Get(dest, params, headers, progress);
        break;
    case HTTP_POST:
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Post(dest, headers, body, content_type, progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for POST yet");
        }
        break;
    case HTTP_PUT:
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Put(dest, headers, body, content_type, progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for PUT yet");
        }
        break;
    case HTTP_PATCH:
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Patch(dest, headers, body, content_type, progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for PATCH yet");
        }
        break;
    case HTTP_DELETE: {
        if (std::holds_alternative<std::string>(test->request.body)) {
            std::string body = std::get<std::string>(test->request.body);
            result = cli.Delete(dest, headers, body, content_type, progress);
        } else {
            // Log(LogLevel::Error, "TODO: Multi Part Body not implemented for DELETE yet");
        }
    } break;
    }
    return result;
}

bool status_match(const std::string& match, int status) noexcept {
    auto status_str = to_string(status);
    for (size_t i = 0; i < std::min(match.size(), 3ul); i++) {
        if (std::tolower(match[i]) == 'x') {
            continue;
        }
        if (match[i] != status_str[i]) {
            return false;
        }
    }
    return true;
}

const char* body_match(const Test* test, const httplib::Result& result) noexcept {
    if (result->has_header("Content-Type")) {
        ContentType to_match = response_content_type(test->response.body_type);

        ContentType content_type = parse_content_type(result->get_header_value("Content-Type"));
        // printf("%s / %s = %s / %s\n", to_match.type.c_str(), to_match.name.c_str(),
        // content_type.type.c_str(), content_type.name.c_str());

        if (to_match != content_type) {
            return "Unexpected Content-Type";
        }

        if (!std::visit(EmptyVisitor(), test->response.body)) {
            if (test->response.body_type == RESPONSE_JSON) {
                assert(std::holds_alternative<std::string>(test->response.body));
                const char* err =
                    json_validate(&std::get<std::string>(test->response.body), &result->body);
                if (err) {
                    return err;
                }
            } else {
                assert(std::holds_alternative<std::string>(test->response.body));
                if (std::get<std::string>(test->response.body) != result->body) {
                    return "Unexpected Body";
                }
            }
        }
    }

    return nullptr;
}

const char* header_match(const Test* test, const httplib::Result& result) noexcept {
    httplib::Headers headers = response_headers(test);
    for (const auto& elem : test->response.cookies.elements) {
        if (elem.enabled) {
            headers.emplace("Set-Cookie", elem.key + "=" + elem.data.data);
        }
    }

    for (const auto& [key, value] : headers) {
        bool found = false;
        for (const auto& [match_key, match_value] : result->headers) {
            if (key == match_key && contains(match_value, value)) {
                found = true;
                break;
            }
        }

        if (!found) {
            return "Unexpected Headers";
        }
    }

    return nullptr;
}

void test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result) noexcept {
    switch (http_result.error()) {
    case httplib::Error::Success: {
        if (!status_match(test->response.status, http_result->status)) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = "Unexpected Status";
            break;
        }

        char const* err = body_match(test, http_result);
        if (err) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        err = header_match(test, http_result);
        if (err) {
            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        test_result->status.store(STATUS_OK);
        test_result->verdict = "Success";
    } break;
    case httplib::Error::Canceled:
        test_result->status.store(STATUS_CANCELLED);
        break;
    default:
        test_result->status.store(STATUS_ERROR);
        test_result->verdict = to_string(http_result.error());
        break;
    }

    test_result->http_result = std::forward<httplib::Result>(http_result);
}

void run_test(AppState* app, const Test* test) noexcept {
    httplib::Result result = make_request(app, test);
    if (!app->test_results.contains(test->id)) {
        return;
    }

    TestResult* test_result = &app->test_results.at(test->id);
    test_result->running.store(false);
    test_analysis(app, test, test_result, std::move(result));
}

void run_tests(AppState* app, const std::vector<Test>* tests) noexcept {
    app->thr_pool.purge();
    app->test_results.clear();
    app->runner_params->dockingParams.dockableWindowOfName("Results")->focusWindowAtNextFrame =
        true;

    for (Test test : *tests) {
        app->test_results.try_emplace(test.id, test);

        // add cli settings from parent to a copy
        test.cli_settings = app->get_cli_settings(test.id);

        app->thr_pool.detach_task([app, test = std::move(test)]() { return run_test(app, &test); });
    }
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

void show_menus(AppState* app) noexcept {
    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Save As", "Ctrl + Shift + S")) {
            save_as_file_dialog(app);
        } else if (ImGui::MenuItem("Save", "Ctrl + S")) {
            save_file_dialog(app);
        } else if (ImGui::MenuItem("Open", "Ctrl + O")) {
            open_file_dialog(app);
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

                if (!(test.flags & TEST_DISABLED) && !app->parent_disabled(&nested_test)) {
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

    // saving
    if (app->open_file_dialog.has_value() && app->open_file_dialog->ready()) {
        auto result = app->open_file_dialog->result();

        if (result.size() > 0) {
            app->filename = result[0];
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->open_file();
        }

        app->open_file_dialog = std::nullopt;
    }

    if (app->save_file_dialog.has_value() && app->save_file_dialog->ready()) {
        if (app->save_file_dialog->result().size() > 0) {
            app->filename = app->save_file_dialog->result();
            Log(LogLevel::Debug, "filename: %s", app->filename.value().c_str());
            app->save_file();
        }

        app->save_file_dialog = std::nullopt;
    }

    // SHORTCUTS
    //
    // saving
    if (io.KeyCtrl && io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_as_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_S)) {
        save_file_dialog(app);
    } else if (io.KeyCtrl && !io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_O)) {
        open_file_dialog(app);
    }

    // undo
    if (app->undo_history.can_undo() && io.KeyCtrl && !io.KeyShift &&
        ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->undo();
    } else if (app->undo_history.can_redo() && io.KeyCtrl && io.KeyShift &&
               ImGui::IsKeyPressed(ImGuiKey_Z)) {
        app->redo();
    }

    // tree view
    if (app->selected_tests.size() > 0) {
        // copy pasting
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

// program leaks those fonts
// can't do much ig and not a big deal
void load_fonts(AppState* app) noexcept {
    app->regular_font =
        HelloImGui::LoadFont("fonts/DroidSans.ttf", 15, {.useFullGlyphRange = true});
    app->regular_font = HelloImGui::MergeFontAwesomeToLastFont(15);
    app->mono_font =
        HelloImGui::LoadFont("fonts/MesloLGS NF Regular.ttf", 15, {.useFullGlyphRange = true});
    app->awesome_font =
        HelloImGui::LoadFont("fonts/fontawesome-webfont.ttf", 15, {.useFullGlyphRange = true});
}

void post_init(AppState* app) noexcept {
    std::string ini = HelloImGui::IniSettingsLocation(*app->runner_params);
    Log(LogLevel::Debug, "Ini: %s", ini.c_str());
    HelloImGui::HelloImGuiIniSettings::LoadHelloImGuiMiscSettings(ini, app->runner_params);
    Log(LogLevel::Debug, "Theme: %s",
        ImGuiTheme::ImGuiTheme_Name(app->runner_params->imGuiWindowParams.tweakedTheme.Theme));

#if !CPPHTTPLIB_OPENSSL_SUPPORT
    Log(LogLevel::Warning, "Compiled without OpenSSL support! HTTPS will not work!");
#endif
#if !CPPHTTPLIB_ZLIB_SUPPORT
    Log(LogLevel::Warning, "Compiled without ZLib support! Zlib compression will not work!");
#endif
#if !CPPHTTPLIB_BROTLI_SUPPORT
    Log(LogLevel::Warning, "Compiled without Brotli support! Brotli compression will not work!");
#endif

    // NOTE: you have to do this in show_gui instead because imgui is stupid
    // ImGuiTheme::ApplyTweakedTheme(app->runner_params->imGuiWindowParams.tweakedTheme);
}

void register_tests(AppState* app) noexcept {
    ImGuiTestEngine* e = HelloImGui::GetImGuiTestEngine();
    const char* root_selectable = "**/##0";

    auto delete_all = [app](ImGuiTestContext* ctx) {
        std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_ids;
        ctx->KeyDown(ImGuiKey_ModCtrl);
        for (size_t id : top_groups) {
            ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
        }
        ctx->KeyUp(ImGuiKey_ModCtrl);
        ctx->ItemClick(("**/##" + to_string(top_groups[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__basic_context = IM_REGISTER_TEST(e, "tree_view", "basic_context");
    tree_view__basic_context->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Copy");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Paste");

        delete_all(ctx);
    };

    ImGuiTest* tree_view__copy_paste = IM_REGISTER_TEST(e, "tree_view", "copy_paste");
    tree_view__copy_paste->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 5; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        delete_all(ctx);
    };

    ImGuiTest* tree_view__ungroup = IM_REGISTER_TEST(e, "tree_view", "ungroup");
    tree_view__ungroup->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 3; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        while (app->tests.size() > 1) {
            std::vector<size_t> top_groups = std::get<Group>(app->tests[0]).children_ids;
            ctx->KeyDown(ImGuiKey_ModCtrl);
            for (size_t id : top_groups) {
                ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
            }
            ctx->KeyUp(ImGuiKey_ModCtrl);
            ctx->ItemClick(("**/##" + to_string(top_groups[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick("**/Ungroup");
        }
    };

    ImGuiTest* tree_view__moving = IM_REGISTER_TEST(e, "tree_view", "moving");
    tree_view__moving->TestFunc = [app, root_selectable, delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Sort");
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;

        // test should be last
        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[1])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[1])).c_str());

        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };
}

int main() {
    HelloImGui::RunnerParams runner_params;
    auto app = AppState(&runner_params);

    runner_params.appWindowParams.windowTitle = "weetee";

    runner_params.imGuiWindowParams.showMenuBar = true;
    runner_params.imGuiWindowParams.showStatusBar = true;
    runner_params.imGuiWindowParams.rememberTheme = true;

    runner_params.imGuiWindowParams.defaultImGuiWindowType =
        HelloImGui::DefaultImGuiWindowType::ProvideFullScreenDockSpace;

    runner_params.callbacks.ShowGui = [&app]() { show_gui(&app); };
    runner_params.callbacks.ShowMenus = [&app]() { show_menus(&app); };
    runner_params.callbacks.LoadAdditionalFonts = [&app]() { load_fonts(&app); };
    runner_params.callbacks.PostInit = [&app]() { post_init(&app); };
    runner_params.callbacks.RegisterTests = [&app]() { register_tests(&app); };

    runner_params.dockingParams = layout(&app);
    runner_params.fpsIdling.enableIdling = false;
    runner_params.useImGuiTestEngine = true;

    ImmApp::AddOnsParams addOnsParams;
    ImmApp::Run(runner_params, addOnsParams);
    return 0;
}

#pragma once

#include "imgui.h"
#include "imgui_stdlib.h"

#include "app_state.hpp"
#include "test.hpp"

#include "cmath"

bool tree_view_context(AppState* app, size_t nested_test_id) noexcept;
bool tree_view_selectable(AppState* app, size_t id, const char* label) noexcept;
bool tree_view_show(AppState* app, NestedTest& nt, float indentation) noexcept;
bool tree_view_show(AppState* app, Test& test, float indentation = 0) noexcept;
bool tree_view_show(AppState* app, Group& group, float indentation = 0) noexcept;
void tree_view(AppState* app) noexcept;

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

bool partial_dict_data_row(AppState*, Cookies*, CookiesElement* elem) noexcept;
bool partial_dict_data_row(AppState*, Parameters*, ParametersElement* elem) noexcept;
bool partial_dict_data_row(AppState*, Headers*, HeadersElement* elem) noexcept;
bool partial_dict_data_row(AppState*, MultiPartBody*, MultiPartBodyElement* elem) noexcept;

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

bool editor_test_request(AppState* app, EditorTab, Test& test) noexcept;
bool editor_test_response(AppState* app, EditorTab, Test& test) noexcept;

enum ModalResult : uint8_t {
    MODAL_NONE,
    MODAL_CONTINUE,
    MODAL_SAVE,
    MODAL_CANCEL,
};

ModalResult unsaved_changes(AppState*) noexcept;

void show_httplib_headers(AppState* app, const httplib::Headers& headers) noexcept;
void show_httplib_cookies(AppState* app, const httplib::Headers& headers) noexcept;

ModalResult open_result_details(AppState* app, const TestResult* tr) noexcept;

enum EditorTabResult : uint8_t {
    TAB_NONE,
    TAB_CLOSED,
    TAB_CHANGED,
};

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab) noexcept;
EditorTabResult editor_tab_group(AppState* app, EditorTab& tab) noexcept;
void tabbed_editor(AppState* app) noexcept;

void testing_results(AppState* app) noexcept;

std::vector<HelloImGui::DockingSplit> splits() noexcept;
std::vector<HelloImGui::DockableWindow> windows(AppState* app) noexcept;
HelloImGui::DockingParams layout(AppState* app) noexcept;

void save_as_file_dialog(AppState* app) noexcept;
void save_file_dialog(AppState* app) noexcept;
void open_file_dialog(AppState* app) noexcept;

void show_menus(AppState* app) noexcept;
void show_gui(AppState* app) noexcept;

// Program leaks those fonts
// can't do much I guess and not a big deal
void load_fonts(AppState* app) noexcept;

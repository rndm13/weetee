#pragma once

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"
#include "imgui_stdlib.h"

#include "app_state.hpp"
#include "imgui_toggle/imgui_toggle.h"
#include "partial_dict.hpp"
#include "tests.hpp"

#include "cmath"

static constexpr ImGuiTableFlags TABLE_FLAGS =
    ImGuiTableFlags_ScrollY | ImGuiTableFlags_BordersV | ImGuiTableFlags_Hideable |
    ImGuiTableFlags_BordersOuter | ImGuiTableFlags_RowBg | ImGuiTableFlags_Reorderable |
    ImGuiTableFlags_Resizable;

static constexpr ImGuiSelectableFlags SELECTABLE_FLAGS = ImGuiSelectableFlags_SpanAllColumns |
                                                         ImGuiSelectableFlags_AllowOverlap |
                                                         ImGuiSelectableFlags_AllowDoubleClick;

static constexpr ImGuiDragDropFlags DRAG_SOURCE_FLAGS =
    ImGuiDragDropFlags_SourceNoDisableHover | ImGuiDragDropFlags_SourceNoHoldToOpenOthers;

#define CHECKBOX_FLAG(flags, changed, flag_name, flag_label)                                       \
    do {                                                                                           \
        bool flag = (flags) & (flag_name);                                                         \
        if (ImGui::Checkbox((flag_label), &flag)) {                                                \
            changed = true;                                                                        \
            if (flag) {                                                                            \
                (flags) |= (flag_name);                                                            \
            } else {                                                                               \
                (flags) &= ~(flag_name);                                                           \
            }                                                                                      \
        }                                                                                          \
    } while (0);

#define COMBO_VARIANT(label, variant, changed, variant_labels, variant_type)                       \
    do {                                                                                           \
        size_t type = (variant).index();                                                           \
        if (ImGui::BeginCombo((label), (variant_labels)[type])) {                                  \
            for (size_t i = 0; i < ARRAY_SIZE((variant_labels)); i++) {                            \
                assert(valid_variant_from_index<variant_type>(i));                                 \
                if (ImGui::Selectable((variant_labels)[i], i == type)) {                           \
                    (changed) = true;                                                              \
                    type = static_cast<size_t>(i);                                                 \
                    (variant) = variant_from_index<variant_type>(type).value();                    \
                }                                                                                  \
            }                                                                                      \
            ImGui::EndCombo();                                                                     \
        }                                                                                          \
    } while (0);

bool arrow(const char* label, ImGuiDir dir);
bool http_type_button(HTTPType type, ImVec2 size = {0, 0});

bool tree_view_context(AppState* app, size_t nested_test_id);
bool tree_view_selectable(AppState* app, size_t id, const char* label);
bool show_tree_view_row(
    AppState* app,
    NestedTest& nt,
    ImVec2& min_selectable_rect,
    ImVec2& max_selectable_rect,
    size_t idx = 0,
    float indentation = 0);
bool show_tree_view_row(
    AppState* app,
    Test& test,
    ImVec2& min_selectable_rect,
    ImVec2& max_selectable_rect,
    size_t idx = 0,
    float indentation = 0);
bool show_tree_view_row(
    AppState* app,
    Group& group,
    ImVec2& min_selectable_rect,
    ImVec2& max_selectable_rect,
    size_t idx = 0,
    float indentation = 0);
void tree_view(AppState* app);

template <typename Data>
bool partial_dict_row(
    AppState* app,
    PartialDict<Data>* pd,
    PartialDictElement<Data>* elem,
    const VariablesMap& vars,
    int32_t flags,
    const char** hints,
    const size_t hint_count) {
    bool changed = false;

    auto select_only_this = [pd, elem]() {
        for (auto& e : pd->elements) {
            e.flags &= ~PARTIAL_DICT_ELEM_SELECTED;
        }
        elem->flags |= PARTIAL_DICT_ELEM_SELECTED;
    };

    if (ImGui::TableNextColumn()) {
        if (flags & PARTIAL_DICT_NO_ENABLE) {
            ImGui::BeginDisabled();
        }
        CHECKBOX_FLAG(elem->flags, changed, PARTIAL_DICT_ELEM_ENABLED, "##enabled");
        if (flags & PARTIAL_DICT_NO_ENABLE) {
            ImGui::EndDisabled();
        }

        ImGui::SameLine();
        if (ImGui::Selectable(
                "##element",
                elem->flags & PARTIAL_DICT_ELEM_SELECTED,
                SELECTABLE_FLAGS,
                ImVec2(0, 0))) {
            auto& io = ImGui::GetIO();
            if (io.KeyCtrl) {
                elem->flags ^= PARTIAL_DICT_ELEM_SELECTED;
            } else if (io.KeyShift) {
                bool selection_start = false;
                for (size_t e_idx = 0; e_idx < pd->elements.size(); e_idx++) {
                    PartialDictElement<Data>* it_elem = &pd->elements.at(e_idx);
                    if (it_elem == elem || e_idx == pd->last_selected_idx) {
                        selection_start = !selection_start;
                    }

                    if (selection_start || it_elem == elem || e_idx == pd->last_selected_idx) {
                        it_elem->flags |= PARTIAL_DICT_ELEM_SELECTED;
                    } else {
                        it_elem->flags &= ~PARTIAL_DICT_ELEM_SELECTED;
                    }
                }
            } else {
                select_only_this();
            }

            // Get index using pointer math
            pd->last_selected_idx = elem - pd->elements.data();
        }

        if (ImGui::BeginPopupContextItem("##partial_dict_context")) {
            if (!(elem->flags & PARTIAL_DICT_ELEM_SELECTED)) {
                select_only_this();
            }

            // TODO: i18n partial_dict context menu

            if (ImGui::MenuItem("Delete", nullptr, false, !(flags & PARTIAL_DICT_NO_DELETE))) {
                changed = true;

                for (auto& e : pd->elements) {
                    if (e.flags & PARTIAL_DICT_ELEM_SELECTED) {
                        e.flags |= PARTIAL_DICT_ELEM_TO_DELETE;
                    }
                }
            }

            if (ImGui::MenuItem("Enable", nullptr, false, !(flags & PARTIAL_DICT_NO_ENABLE))) {
                for (auto& e : pd->elements) {
                    if (e.flags & PARTIAL_DICT_ELEM_SELECTED) {
                        e.flags |= PARTIAL_DICT_ELEM_ENABLED;
                    }
                }
            }

            if (ImGui::MenuItem("Disable", nullptr, false, !(flags & PARTIAL_DICT_NO_ENABLE))) {
                for (auto& e : pd->elements) {
                    if (e.flags & PARTIAL_DICT_ELEM_SELECTED) {
                        e.flags &= ~PARTIAL_DICT_ELEM_ENABLED;
                    }
                }
            }

            if constexpr (requires { partial_dict_data_context(app, pd, elem, vars); }) {
                ImGui::Separator();
                changed |= partial_dict_data_context(app, pd, elem, vars);
            }

            ImGui::EndPopup();
        }
    }

    if (ImGui::TableNextColumn()) { // Name
        // If no key change is set, can use regular text input
        if (hint_count > 0 && !(flags & PARTIAL_DICT_NO_KEY_CHANGE)) {
            assert(hints);
            if (!elem->cfs) {
                elem->cfs = ComboFilterState{};
            }
            ImGui::SetNextItemWidth(-1);
            changed |= ComboFilter("##name", &elem->key, hints, hint_count, &elem->cfs.value());
        } else {
            ImGui::SetNextItemWidth(-1);
            ImGui::BeginDisabled(flags & PARTIAL_DICT_NO_KEY_CHANGE);
            changed |= ImGui::InputText("##name", &elem->key);
            ImGui::EndDisabled();
        }
    }

    changed |= partial_dict_data_row(app, pd, elem, vars);
    return changed;
}

bool partial_dict_data_row(AppState*, Cookies*, CookiesElement* elem, const VariablesMap&);
bool partial_dict_data_row(AppState*, Parameters*, ParametersElement* elem, const VariablesMap&);
bool partial_dict_data_row(AppState*, Headers*, HeadersElement* elem, const VariablesMap&);
bool partial_dict_data_row(AppState*, Variables*, VariablesElement* elem, const VariablesMap&);
bool partial_dict_data_context(AppState*, Variables*, VariablesElement* elem, const VariablesMap&);
bool partial_dict_data_row(
    AppState*, MultiPartBody*, MultiPartBodyElement* elem, const VariablesMap&);

template <typename Data>
bool partial_dict(
    AppState* app,
    PartialDict<Data>* pd,
    const char* label,
    const VariablesMap& vars,
    uint8_t flags = PARTIAL_DICT_NONE,
    const char** hints = nullptr,
    const size_t hint_count = 0) {
    using DataType = typename PartialDict<Data>::DataType;

    bool changed = false;

    //              name and checkbox        additional
    int32_t field_count = 2 + DataType::field_count;
    auto data_fields = DataType::field_labels(&app->i18n);
    if (ImGui::BeginTable(label, field_count, TABLE_FLAGS, ImVec2(0, 300))) {
        ImGui::TableSetupColumn(" ", ImGuiTableColumnFlags_WidthFixed, 17.0f);
        ImGui::TableSetupColumn(app->i18n.ed_pd_name.c_str());
        for (size_t i = 0; i < DataType::field_count; i++) {
            ImGui::TableSetupColumn(data_fields[i]);
        }
        ImGui::TableHeadersRow();
        bool deletion = false;

        for (size_t i = 0; i < pd->elements.size(); i++) {
            auto* elem = &pd->elements[i];
            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(i));

            uint8_t row_flags = flags | (elem->flags & PARTIAL_DICT_ELEM_REQUIRED
                                             ? PARTIAL_DICT_NO_ENABLE | PARTIAL_DICT_NO_DELETE |
                                                   PARTIAL_DICT_NO_KEY_CHANGE
                                             : PARTIAL_DICT_NONE);

            changed |= partial_dict_row(app, pd, elem, vars, row_flags, hints, hint_count);
            deletion |= elem->flags & PARTIAL_DICT_ELEM_TO_DELETE;
            ImGui::PopID();
        }

        if (deletion) {
            changed = true;

            pd->elements.erase(
                std::remove_if(pd->elements.begin(), pd->elements.end(), [](const auto& elem) {
                    return elem.flags & PARTIAL_DICT_ELEM_TO_DELETE;
                }));
        }

        if (!(flags & PARTIAL_DICT_NO_CREATE)) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn(); // enabled, skip
            if (ImGui::TableNextColumn()) {
                ImGui::Text("%s", app->i18n.ed_pd_change_hint.c_str());
            }

            ImGui::TableNextRow();
            ImGui::PushID(static_cast<int32_t>(pd->elements.size()));

            uint8_t row_flags = flags | PARTIAL_DICT_NO_ENABLE | PARTIAL_DICT_NO_DELETE;

            if (partial_dict_row(app, pd, &pd->add_element, vars, row_flags, hints, hint_count)) {
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

bool editor_test_request(AppState* app, Test& test);
bool editor_test_response(AppState* app, Test& test);

enum ModalResult : uint8_t {
    MODAL_NONE,
    MODAL_CONTINUE,
    MODAL_SAVE,
    MODAL_CANCEL,
};

ModalResult unsaved_changes(AppState*);

bool editor_auth(const std::string& label, const I18N*, AuthVariant* auth);
bool editor_client_settings(const I18N* i18n, ClientSettings* set, bool show_dynamic);

void show_httplib_headers(AppState* app, const httplib::Headers& headers);
void show_httplib_cookies(AppState* app, const httplib::Headers& headers);

ModalResult open_result_details(AppState* app, TestResult* tr);

enum EditorTabResult : uint8_t {
    TAB_NONE,
    TAB_CLOSED,
    TAB_CHANGED,
};

EditorTabResult editor_tab_test(AppState* app, EditorTab& tab);
EditorTabResult editor_tab_group(AppState* app, EditorTab& tab);
void tabbed_editor(AppState* app);

void testing_results(AppState* app);

std::vector<HelloImGui::DockingSplit> splits();
std::vector<HelloImGui::DockableWindow> windows(AppState* app);
HelloImGui::DockingParams layout(AppState* app);

void save_as_file_dialog(AppState* app);
void save_file_dialog(AppState* app);
void open_file_dialog(AppState* app);

void show_menus(AppState* app);
void show_app_menu_items(AppState* app);
void show_gui(AppState* app);

void pre_frame(AppState* app);

// Program leaks those fonts
// can't do much I guess and not a big deal
void load_fonts(AppState* app);

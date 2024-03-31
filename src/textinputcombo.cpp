#include "textinputcombo.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "imgui_stdlib.h"

// imgui combo filter v1.0, by @r-lyeh (public domain)
// contains *modified* code by @harold-b (public domain?)

bool ComboFilter__DrawPopup(
    ComboFilterState* state,
    int START, const char** ENTRIES) {
    bool clicked = 0;

    // Grab the position for the popup
    ImVec2 pos = ImGui::GetItemRectMin();
    pos.y += ImGui::GetItemRectSize().y;
    ImVec2 size = ImVec2(ImGui::GetItemRectSize().x - 60, ImGui::GetTextLineHeightWithSpacing() * 4);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0);

    ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoTitleBar |
        ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoMove |
        ImGuiWindowFlags_HorizontalScrollbar |
        ImGuiWindowFlags_NoSavedSettings |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoDocking |
        0; // ImGuiWindowFlags_ShowBorders;

    // ImGui::SetNextWindowFocus();
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    ImGui::Begin("##combo_filter", nullptr, flags);
    ImGui::PushAllowKeyboardFocus(false);
    for (int i = 0; i < state->num_hints; i++) {
        // Track if we're drawing the active index so we
        // can scroll to it if it has changed
        bool isIndexActive = state->activeIdx == i;

        if (isIndexActive) {
            // Draw the currently 'active' item differently
            // ( used appropriate colors for your own style )
            ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1, 1, 0, 1));
        }

        ImGui::PushID(i);
        if (ImGui::Selectable(ENTRIES[i], isIndexActive)) {
            // And item was clicked, notify the input
            // callback so that it can modify the input buffer
            state->activeIdx = i;
            clicked = 1;
        }

        if (ImGui::IsItemFocused() && ImGui::IsKeyPressed(ImGuiKey_Enter)) {
            // Allow ENTER key to select current highlighted item (w/ keyboard navigation)
            state->activeIdx = i;
            clicked = 1;
        }
        ImGui::PopID();

        if (isIndexActive) {
            if (state->selectionChanged) {
                // Make sure we bring the currently 'active' item into view.
                ImGui::SetScrollHereY();
                // SetScrollHereX();
                state->selectionChanged = false;
            }

            ImGui::PopStyleColor(1);
        }
    }

    ImGui::PopAllowKeyboardFocus();
    ImGui::End();
    ImGui::PopStyleVar(1);

    return clicked;
}

// just sets the text changed flag
int ComboFilter__TextCallback(ImGuiInputTextCallbackData* data) {
    ComboFilterState* state = (ComboFilterState*)data->UserData;
    state->textChanged |= data->EventFlag & ImGuiInputTextFlags_CallbackEdit;
    state->historyMove |= data->EventKey == ImGuiKey_UpArrow || data->EventKey == ImGuiKey_DownArrow;
    state->historyMove &= !(data->EventFlag & ImGuiInputTextFlags_CallbackEdit); // reset after edit

    state->selectionChanged = data->EventKey == ImGuiKey_UpArrow || data->EventKey == ImGuiKey_DownArrow;

    if (data->EventKey == ImGuiKey_UpArrow && state->activeIdx > 0) {
        state->activeIdx -= 1;
    }
    if (data->EventKey == ImGuiKey_DownArrow && state->activeIdx < state->num_hints - 1) {
        state->activeIdx += 1;
    }

    return 0;
}

bool ComboFilter(
    const char* id, std::string* str,
    const char** hints, ComboFilterState* s) {
    struct fuzzy {
        static int score(const char* str1, const char* str2) {
            int score = 0, consecutive = 0, maxerrors = 0;
            while (*str1 && *str2) {
                int is_leading = (*str1 & 64) && !(str1[1] & 64);
                if ((*str1 & ~32) == (*str2 & ~32)) {
                    int had_separator = (str1[-1] <= 32);
                    int x = had_separator || is_leading ? 10 : consecutive * 5;
                    consecutive = 1;
                    score += x;
                    ++str2;
                } else {
                    int x = -1, y = is_leading * -3;
                    consecutive = 0;
                    score += x;
                    maxerrors += y;
                }
                ++str1;
            }
            return score + (maxerrors < -9 ? -9 : maxerrors);
        }
        static int search(const char* str, int num, const char* words[]) {
            int scoremax = 0;
            int best = -1;
            for (int i = 0; i < num; ++i) {
                int score = fuzzy::score(words[i], str);
                int record = (score >= scoremax);
                int draw = (score == scoremax);
                if (record) {
                    scoremax = score;
                    if (!draw)
                        best = i;
                    else
                        best = best >= 0 && strlen(words[best]) < strlen(words[i]) ? best : i;
                }
            }
            return best;
        }
    };

    bool done = ImGui::InputText(
        id, str,
        ImGuiInputTextFlags_AutoSelectAll | ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackEdit | ImGuiInputTextFlags_CallbackHistory,
        ComboFilter__TextCallback, s);
    const auto input_text_id = ImGui::GetItemID();

    s->textChanged &= ImGui::IsItemFocused();
    bool hot = s->textChanged && s->activeIdx >= 0 && *str != hints[s->activeIdx];
    if (hot) {
        int new_idx = fuzzy::search(str->c_str(), s->num_hints, hints);
        int idx = !s->historyMove && new_idx >= 0 ? new_idx : s->activeIdx;

        s->selectionChanged = s->selectionChanged || s->activeIdx != idx;
        s->activeIdx = idx;
        if (done || ComboFilter__DrawPopup(s, idx, hints)) {
            int i = s->activeIdx;
            if (i >= 0) {
                *str = hints[i];
                done = true;
            }
        }

        ImGui::SetFocusID(input_text_id, ImGui::GetCurrentWindow());
    }
    return done;
}

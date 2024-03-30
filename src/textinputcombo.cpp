// Got this code from: https://github.com/ocornut/imgui/issues/2057#issue-356321758
// Slightly modified it
//
#include "textinputcombo.hpp"

namespace ImGui {
bool identical(const char* buf, const char* item) {
    for (int i = 0; buf[i] && item[i]; ++i) {
        // set the current pos if matching or return the pos if not
        if (buf[i] != item[i]) {
            return false;
        }
    }
    // Complete match
    // and the item size is greater --> include
    return true;
};

int propose(ImGuiInputTextCallbackData* data) {
    // We don't want to "preselect" anything
    if (strlen(data->Buf) == 0) {
        return 0;
    }

    // Get our items back
    const char** items = static_cast<std::pair<const char**, size_t>*>(data->UserData)->first;
    size_t length = static_cast<std::pair<const char**, size_t>*>(data->UserData)->second;

    // We need to give the user a chance to remove wrong input
    if (ImGui::IsKeyDown(ImGuiKey_Backspace)) {
        // We delete the last char automatically, since it is what the user wants to delete, but only if there is something (selected/marked/hovered)
        if (data->BufTextLen > 0 && data->CursorPos > 0) {
            if (data->SelectionEnd != data->SelectionStart) {
                data->DeleteChars(data->CursorPos - 1, 1);
            }
        }
        return 0;
    }
    if (ImGui::IsKeyDown(ImGuiKey_Delete)) {
        return 0;
    }

    for (int i = 0; i < length; i++) {
        if (identical(data->Buf, items[i])) {
            const int cursor = data->CursorPos;
            // Insert the first match
            data->DeleteChars(0, data->BufTextLen);
            data->InsertChars(0, items[i]);
            // Reset the cursor position
            data->CursorPos = cursor;
            // Select the text, so the user can simply go on writing
            data->SelectionStart = cursor;
            data->SelectionEnd = data->BufTextLen;
            break;
        }
    }
    return 0;
}

bool InputTextCombo(const char* id, std::string* str, size_t maxInputSize, const char* items[], size_t item_len, short showMaxItems) {
    if (str->size() > maxInputSize) { // too large for editing
        ImGui::Text("%s", str->c_str());
        return false;
    }

    std::string buffer(*str);
    buffer.resize(maxInputSize);
    bool changed = ImGui::InputTextCombo(id, &buffer[0], maxInputSize, items, item_len, showMaxItems);
    // using string as char array
    if (changed) {
        auto i = buffer.find_first_of('\0');
        *str = buffer.substr(0u, i);
    }
    return changed;
}

// Creates a ComboBox with free text input and completion proposals
// Pass your items via items
// showMaxItems determines how many items are shown, when the dropdown is open; if 0 is passed the complete list will be shown; you will want normaly a value of 8
bool InputTextCombo(const char* id, char* buffer, size_t maxInputSize, const char* items[], size_t item_len, short showMaxItems) {
    // Check if both strings matches
    if (showMaxItems == 0) {
        showMaxItems = item_len;
    }

    ImGui::PushID(id);
    std::pair<const char**, size_t> pass(items, item_len); // We need to pass the array length as well

    bool ret = ImGui::InputText("##in", buffer, maxInputSize, ImGuiInputTextFlags_::ImGuiInputTextFlags_CallbackAlways, propose, static_cast<void*>(&pass));

    ImGui::OpenPopupOnItemClick("combobox"); // Enable right-click
    ImVec2 pos = ImGui::GetItemRectMin();
    ImVec2 size = ImGui::GetItemRectSize();

    ImGui::SameLine(0, 0);
    if (ImGui::ArrowButton("##openCombo", ImGuiDir_Down)) {
        ImGui::OpenPopup("combobox");
    }
    ImGui::OpenPopupOnItemClick("combobox"); // Enable right-click

    pos.y += size.y;
    size.x += ImGui::GetItemRectSize().x;
    size.y += 5 + (size.y * showMaxItems);
    ImGui::SetNextWindowPos(pos);
    ImGui::SetNextWindowSize(size);
    if (ImGui::BeginPopup("combobox", ImGuiWindowFlags_::ImGuiWindowFlags_NoMove)) {
        ImGui::Text("Select one item or type");
        ImGui::Separator();
        for (int i = 0; i < item_len; i++) {
            if (ImGui::Selectable(items[i])) {
                strcpy(buffer, items[i]);
                ret = true;
            }
        }

        ImGui::EndPopup();
    }
    ImGui::PopID();

    ImGui::SameLine(0, ImGui::GetStyle().ItemInnerSpacing.x);
    ImGui::Text("%s", id);

    return ret;
}
} // namespace ImGui

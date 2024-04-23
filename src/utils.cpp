#include "utils.hpp"

bool arrow(const char* label, ImGuiDir dir) noexcept {
    assert(label);
    ImGui::PushStyleColor(ImGuiCol_Button, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_Border, 0x00000000);
    ImGui::PushStyleColor(ImGuiCol_BorderShadow, 0x00000000);
    bool result = ImGui::ArrowButton(label, dir);
    ImGui::PopStyleColor(5);
    return result;
}

void remove_arrow_offset() noexcept { ImGui::SetCursorPosX(ImGui::GetCursorPosX() - 8); }

bool contains(const std::string& haystack, const std::string& needle) noexcept {
    size_t need_idx = 0;
    for (char hay : haystack) {
        if (std::tolower(hay) == std::tolower(needle[need_idx])) {
            need_idx++;
        }
        if (need_idx >= needle.size()) {
            return true;
        }
    }
    return false;
}

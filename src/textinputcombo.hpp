// Got this code from: https://github.com/ocornut/imgui/issues/2057#issue-356321758
// Slightly modified it
//
#pragma once

#include "imgui.h"

namespace ImGui {
// extern sf::Keyboard::Key; //This key is set by SFML. I had trouble using the correct ImGui Keys, since I only got Count as Key. See the TODOs in propose()

bool identical(const char* buf, const char* item);
int propose(ImGuiInputTextCallbackData* data);
bool InputTextCombo(const char* id, std::string* str, size_t maxInputSize, const char* items[], size_t item_len, short showMaxItems);
bool InputTextCombo(const char* id, char* buffer, size_t maxInputSize, const char* items[], size_t item_len, short showMaxItems);
} // namespace ImGui

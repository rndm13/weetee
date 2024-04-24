#include "utils.hpp"
#include <httplib.h>

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

std::string file_name(const std::string& path) noexcept {
    // TODO: fix for windows
    size_t slash = path.rfind("/");
    if (slash == std::string::npos) {
        slash = 0;
    } else {
        slash += 1; // skip over it
    }
    return path.substr(slash);
}

std::vector<std::string> split_string(const std::string& str, const std::string& separator) noexcept {
    std::vector<std::string> result;
    size_t index = 0;

    auto push_result = [&result, &str](size_t begin, size_t end) {
        assert(begin < str.size());
        assert(end < str.size());

        // Trimming
        while (begin < str.size() && std::isspace(str.at(begin))) {
            begin += 1;
        }
        // End overflows
        while (end < str.size() && std::isspace(str.at(end))) {
            end -= 1;
        }

        result.push_back(str.substr(begin, end - begin + 1));
    };

    do {
        size_t comma = str.find(separator, index);
        if (comma == std::string::npos) {
            push_result(index, str.size() - 1);
            break;
        }
        push_result(index, comma - 1);
        index = comma + 1;
    } while (index < str.size());

    return result;
}

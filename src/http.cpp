#include "http.hpp"

bool http_type_button(HTTPType type) noexcept {
    ImGui::PushStyleColor(ImGuiCol_Button, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, HTTPTypeColor[type]);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, HTTPTypeColor[type]);
    bool result = ImGui::SmallButton(HTTPTypeLabels[type]);
    ImGui::PopStyleColor(3);
    return result;
}

HTTPType http_type_from_label(std::string label) noexcept {
    std::for_each(label.begin(), label.end(), [](char& c) {c = static_cast<char>(std::toupper(c));});

    for (size_t i = 0; i < ARRAY_SIZE(HTTPTypeLabels); ++i) {
        if (HTTPTypeLabels[i] == label) {
            return static_cast<HTTPType>(i);
        }
    }

    return static_cast<HTTPType>(-1);
}

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

std::pair<std::string, std::string> split_endpoint(std::string endpoint) noexcept {
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

std::string to_string(const ContentType& cont) noexcept { return cont.type + '/' + cont.name; }

ContentType parse_content_type(std::string input) noexcept {
    size_t slash = input.find("/");
    if (slash == std::string::npos) {
        return {"*", "*"};
    }

    size_t end = input.find(";");

    std::string type = input.substr(0, slash);
    std::string name = input.substr(slash + 1, end - (slash + 1));

    return {.type = type, .name = name};
}

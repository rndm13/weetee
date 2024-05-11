#include "json.hpp"
using json = nlohmann::json;

const char* json_format(std::string& input) noexcept {

    json js = json::parse(input, nullptr, false);
    if (js.is_discarded()) {
        return "Invalid JSON";
    }
    input = js.dump(4);
    return nullptr;
}

const char* json_validate(const std::string& expected, const std::string& got) noexcept {
    json json_expected, json_got;

    json_expected = json::parse(expected, nullptr, false);
    if (json_expected.is_discarded()) {
        return "Invalid Expected JSON";
    }

    json_got = json::parse(got, nullptr, false);
    if (json_got.is_discarded()) {
        return "Invalid Response JSON";
    }

    if (json_expected != json_got) {
        return "Unexpected Response JSON";
    }
    
    return nullptr;
}

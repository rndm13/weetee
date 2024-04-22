#include "json.hpp"
#include "../external/json/include/nlohmann/json.hpp"
using json = nlohmann::json;

const char* json_format(std::string* input) noexcept {
    try {
        *input = json::parse(*input).dump(4);
    } catch (json::parse_error& error) {
        return error.what();
    }
    return nullptr;
}

const char* json_validate(const std::string* expected, const std::string* got) noexcept {
    json json_expected, json_got;

    try {
        json_expected = json::parse(*expected);
    } catch (json::parse_error& error) {
        return error.what();
    }

    try {
        json_got = json::parse(*got);
    } catch (json::parse_error& error) {
        return error.what();
    }

    if (json_expected != json_got) {
        return "Unexpected JSON";
    }
    
    return nullptr;
}

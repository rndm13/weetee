#include "json.hpp"
#include "../external/json/include/nlohmann/json.hpp"
using json = nlohmann::json;

const char* json_format(std::string* json) noexcept {
    try {
        *json = json::parse(*json).dump(4);
    } catch (json::parse_error& error) {
        return error.what();
    }
    return nullptr;
}

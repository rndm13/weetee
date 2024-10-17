#include "variables.hpp"

using json = nlohmann::json;

std::string replace_variables(const VariablesMap& vars, const std::string& target) {
    std::string result = target;

    bool changed = true;
    size_t iterations = 0;
    while (iterations < REPLACE_VARIABLES_MAX_NEST && changed) {
        std::vector<std::pair<size_t, size_t>> params_idx = encapsulation_ranges(result, '{', '}');

        changed = false;
        iterations++;

        for (auto it = params_idx.rbegin(); it != params_idx.rend(); it++) {
            auto [idx, size] = *it;
            std::string name = result.substr(idx + 1, size - 1);

            if (vars.contains(name)) {
                changed = true;

                std::string value = vars.at(name);
                if (value.empty()) {
                    value = "<empty>";
                }

                result.replace(idx, size + 1, value);
            }
        }
    }

    return result;
}

json pack_variables(std::string str, const VariablesMap& vars) {
    std::vector<std::pair<size_t, size_t>> var_ranges = encapsulation_ranges(str, '{', '}');

    for (size_t i = 0; i < var_ranges.size(); i++) {
        auto [idx, size] = var_ranges[i];

        std::string name = str.substr(idx + 1, size - 1);

        if (!vars.contains(name)) {
            continue;
        }

        static constexpr const char variable_key[] = "\"" WEETEE_VARIABLE_KEY "\":";

        // Wrap variable name in quotes
        str.insert(idx + size, "\"");
        str.insert(idx + 1, "\"");

        // Add a key
        str.insert(idx + 1, variable_key);
    } 

    json result = json::parse(str, nullptr, false);

    if (result.is_discarded()) {
        printf("%s\n", str.c_str());
    }

    return result;
}

std::string unpack_variables(const json& schema, size_t indent) {
    std::string schema_str;
    schema_str = schema.dump(indent);

    std::string result = "";

    size_t idx = 0;
    while (idx < schema_str.size()) {
        static constexpr const char variable_key[] = "\"" WEETEE_VARIABLE_KEY "\":";
        size_t object_key = schema_str.find(variable_key, idx);
        if (object_key == std::string::npos) {
            result += schema_str.substr(idx);
            break;
        }

        size_t object_begin = schema_str.rfind("{", object_key);
        size_t object_end = schema_str.find("}", object_key);

        size_t var_begin = schema_str.find('"', object_key + ARRAY_SIZE(variable_key)) + 1;
        size_t var_end = schema_str.rfind('"', object_end);

        result += schema_str.substr(idx, object_begin - idx);
        result += "{" + schema_str.substr(var_begin, var_end - var_begin) + "}";

        idx = object_end + 1;
    }

    return result;
}

const char* json_format_variables(std::string& input, const VariablesMap& vars) {
    json j = pack_variables(input, vars);

    if (j.is_discarded()) {
        return "Invalid JSON";
    }

    input = unpack_variables(j, 4);
    return nullptr;
}

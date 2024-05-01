#include "app_state.hpp"
#include "http.hpp"
#include "nlohmann/json.hpp"

using nljson = nlohmann::json;

std::string replace_swagger_variables(const std::string& swagger_vars) noexcept {
    size_t idx = 0;
    std::string result = swagger_vars;
    do {
        size_t left = result.find("{", idx);
        if (left == std::string::npos) {
            break;
        }
        size_t right = result.find("}", left);
        if (right == std::string::npos) {
            break;
        }

        result[left] = '<';
        result[right] = '>';

        idx = right + 1;
    } while (idx < swagger_vars.size());

    return result;
}

void AppState::import_swagger_servers(const nljson& servers) noexcept {
    if (servers.size() <= 0) {
        Log(LogLevel::Warning, "Failed to import swagger servers");
        return;
    }

    const auto& server = servers[0];
    if (server.contains("url")) {
        std::string url = replace_swagger_variables(server["url"]);

        Group* root = this->root_group();
        root->variables->elements.push_back(VariablesElement{
            .key = "url",
            .data = {.data = url},
        });
    } else {
        Log(LogLevel::Warning, "Failed to import swagger server url");
    }

    if (server.contains("variables")) {
        const nljson& vars = server["variables"];

        Group* root = this->root_group();
        for (auto it = vars.begin(); it != vars.end(); it++) {
            std::string key = it.key();
            const nljson& value = it.value();

            // NOTE: Missing enum and description
            if (value.contains("default")) {
                root->variables->elements.push_back(VariablesElement{
                    .key = it.key(),
                    .data = {.data = replace_swagger_variables(value["default"])},
                });
            }
        }
    }

    // NOTE: Missing description
}

void AppState::import_swagger_paths(const nljson& paths) noexcept {
    for (auto path = paths.begin(); path != paths.end(); path++) {
        std::string endpoint = "<url>" + path.key();

        auto group_id = ++this->id_counter;
        auto new_group = Group{
            .parent_id = 0,
            .id = group_id,
            .flags = GROUP_OPEN,
            .name = endpoint,
            .cli_settings = {},
            .children_ids = {},
            .variables = {},
        };

        auto operations = path.value();
        for (auto op = operations.begin(); op != operations.end(); op++) {
            HTTPType type = http_type_from_label(op.key());
            if (type == static_cast<HTTPType>(-1)) {
                continue; // Skip over unknown
            }

            auto test_id = ++this->id_counter;
            auto new_test = Test{
                .parent_id = group_id,
                .id = test_id,
                .flags = TEST_NONE,

                .type = type,
                .endpoint = endpoint,

                .request = {},
                .response = {},

                .cli_settings = {},
            };
            test_resolve_url_params(&new_test);

            new_group.children_ids.push_back(test_id);
            this->tests.emplace(test_id, new_test);
        }

        this->root_group()->children_ids.push_back(group_id);
        this->tests.emplace(group_id, new_group);
    }
}

void AppState::import_swagger(const std::string& swagger_file) noexcept {
    std::ifstream in(swagger_file);
    if (!in) {
        Log(LogLevel::Error, "Failed to open file %s", swagger_file.c_str());
        return;
    }

    try {
        nljson swagger = nljson::parse(in);
        this->selected_tests.clear();
        this->tests = {
            {0, root_initial},
        };

        this->filename = std::nullopt;
        this->id_counter = 0;
        this->test_results.clear();
        this->filtered_tests.clear();

        // pushes initial undo state
        this->undo_history.reset_undo_history(this);

        if (swagger.contains("info")) {
            if (swagger["info"].contains("title")) {
                Group* root = this->root_group();
                root->name = swagger["info"]["title"];
            }
        }

        if (swagger.contains("servers")) {
            import_swagger_servers(swagger["servers"]);
        } else {
            Log(LogLevel::Warning, "Failed to import swagger servers");
        }

        if (swagger.contains("paths")) {
            import_swagger_paths(swagger["paths"]);
        } else {
            Log(LogLevel::Warning, "Failed to import swagger paths");
        }
    } catch (nljson::parse_error& pe) {
        Log(LogLevel::Error, "Failed to parse file %s: %s", swagger_file.c_str(), pe.what());
    } catch (std::exception& e) {
        Log(LogLevel::Error, "Failed to import swagger: %s", swagger_file.c_str(), e.what());
    }
}

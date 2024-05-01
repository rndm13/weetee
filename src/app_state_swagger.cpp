#include "app_state.hpp"
#include "http.hpp"
#include "nlohmann/json.hpp"
#include "test.hpp"

using nljson = nlohmann::json;

void AppState::import_swagger_servers(const nljson& servers) noexcept {
    if (servers.size() <= 0) {
        Log(LogLevel::Warning, "Failed to import swagger servers");
        return;
    }

    const auto& server = servers[0];
    if (server.contains("url")) {
        std::string url = server["url"];

        Group* root = this->root_group();
        root->variables.elements.push_back(VariablesElement{
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
                root->variables.elements.push_back(VariablesElement{
                    .key = it.key(),
                    .data = {.data = value["default"]},
                });
            }
        }
    }

    // NOTE: Missing description
}

void AppState::import_swagger_paths(const nljson& paths) noexcept {
    for (auto path = paths.begin(); path != paths.end(); path++) {
        std::string endpoint = "{url}" + path.key();

        auto group_id = ++this->id_counter;
        {
            auto new_group = Group{
                .parent_id = 0,
                .id = group_id,
                .flags = GROUP_OPEN,
                .name = endpoint,
                .cli_settings = {},
                .children_ids = {},
                .variables = {},
            };
            this->root_group()->children_ids.push_back(group_id);
            this->tests.emplace(group_id, new_group);
        }
        Group& new_group = std::get<Group>(this->tests.at(group_id));

        // TODO: Servers, Parameters

        auto group_vars = this->variables(group_id);
        const auto& operations = path.value();
        for (auto op = operations.begin(); op != operations.end(); op++) {
            HTTPType type = http_type_from_label(op.key());
            if (type == static_cast<HTTPType>(-1)) {
                continue; // Skip over unknown
            }

            auto test_id = ++this->id_counter;
            {
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

                new_group.children_ids.push_back(test_id);
                this->tests.emplace(test_id, new_test);
            }
            Test& new_test = std::get<Test>(this->tests.at(test_id));

            // Variables/Parameters

            if (op.value().contains("parameters")) {
                for (const auto& param : op.value().at("parameters")) {
                    if (!param.contains("name") || !param.contains("in")) {
                        continue;
                    }

                    std::string name = param.at("name");
                    std::string in = param.at("in");

                    std::string value = "";

                    if (param.contains("example")) {
                        value = to_string(param.at("example"));
                    } else if (param.contains("examples")) {
                        const auto& examples = param.at("examples");
                        // TODO: Multiple tests for each example
                        if (examples.size() > 0) {
                            if (examples.at(0).contains("value")) {
                                value = examples.at(0).at("value");
                            }
                        }
                    } else if (param.contains("schema")) {
                        value = to_string(param.at("schema"));
                    }

                    // TODO: Explode

                    // Don't put required on this one as it won't remain required after url
                    // variables resolve and you should provide a way to change it for user
                    if (in == "query") {
                        new_test.request.parameters.elements.push_back({
                            .key = name,
                            .data = {.data = value},
                        });
                    } else {
                        new_test.variables.elements.push_back({
                            .key = name,
                            .data = {.data = value},
                        });
                    }
                }
            }

            test_resolve_url_variables(group_vars, &new_test);

            // Body 
        
            if (op.value().contains("requestBody")) {
                if (op.value().at("requestBody").contains("content")) {
                    const auto& content = op.value().at("requestBody").at("content");
                    for (auto it = content.begin(); it != content.end(); it++) {
                        std::string content_type = it.key();
                        const auto& media_type = it.value();

                        new_test.request.body_type = request_body_type(content_type);
                        if (new_test.request.body_type == REQUEST_OTHER) {
                            new_test.request.other_content_type = content_type;
                        }

                        if (media_type.contains("value")) {
                            new_test.request.body = media_type.at("value").dump(4);
                            if (new_test.request.body_type == REQUEST_MULTIPART) {
                                request_body_convert<REQUEST_MULTIPART>(&new_test);
                            }
                        }

                        // TODO: Multiple tests for each content-type
                        //
                        // Right now only use the first provided
                        break;
                    }
                }
            }
        }
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
        this->opened_editor_tabs.clear();

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

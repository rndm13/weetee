#include "fstream"

#include "json.hpp"
#include "app_state.hpp"
#include "http.hpp"
#include "partial_dict.hpp"
#include "tests.hpp"
#include "utils.hpp"

using nljson = nlohmann::json;

void AppState::import_swagger_servers(const nljson& servers) {
    if (servers.size() <= 0) {
        Log(LogLevel::Warning, "Failed to import swagger servers");
        return;
    }

    const auto& server = servers[0];
    if (server.contains("url")) {
        std::string url = server.at("url");

        Group* root = this->root_group();
        root->variables.elements.push_back(VariablesElement{
            .key = "url",
            .data = {.data = url},
        });
    } else {
        Log(LogLevel::Warning, "Failed to import swagger server url");
    }

    if (server.contains("variables")) {
        const nljson& vars = server.at("variables");

        Group* root = this->root_group();
        for (auto it = vars.begin(); it != vars.end(); it++) {
            std::string key = it.key();
            const nljson& value = it.value();

            // NOTE: Missing enum and description
            if (value.contains("default")) {
                root->variables.elements.push_back(VariablesElement{
                    .key = it.key(),
                    .data = {.data = value.at("default")},
                });
            }
        }
    }

    // NOTE: Missing description
}

nljson resolve_swagger_ref(const nljson& relative, const nljson& swagger) {
    if (!relative.contains("$ref")) {
        return relative;
    }

    std::string ref = relative.at("$ref");
    std::string location, local;
    nljson resolved;

    {
        size_t separator = ref.find("#/");
        if (separator != std::string::npos) {
            location = ref.substr(0, separator);
            local = ref.substr(separator + 2);
            resolved = swagger;
        }
    }

    {
        size_t separator = ref.find("~/");
        if (separator != std::string::npos) {
            location = ref.substr(0, separator);
            local = ref.substr(separator + 2);
            resolved = relative;
        }
    }

    resolved.erase("$ref");

    if (location.empty() && local.empty()) {
        Log(LogLevel::Error, "Failed to resolve swagger Reference '%s'", ref.c_str());
        return {};
    }

    if (location.find("//") != std::string::npos) {
        // URL Reference
        auto [host, endpoint] = split_endpoint(location);
        httplib::Client cli(host);

        auto result = cli.Get(endpoint);
        if (result.error() != httplib::Error::Success) {
            Log(LogLevel::Error, "Failed to resolve swagger URL Reference '%s' : %s",
                location.c_str(), to_string(result.error()).c_str());
            return {};
        } else {
            nljson json = nljson::parse(result->body, nullptr, false);
            if (json.is_discarded()) {
                Log(LogLevel::Error, "Failed to parse swagger URL Reference '%s'",
                    location.c_str());
                return {};
            }

            resolved = json;
        }
    }

    if (!location.empty()) {
        // File

        std::ifstream in(location);
        if (!in) {
            Log(LogLevel::Error, "Failed to open swagger Remote Reference '%s'", location.c_str());
            return {};
        }

        nljson json = nljson::parse(in, nullptr, false);
        if (json.is_discarded()) {
            Log(LogLevel::Error, "Failed to parse swagger Remote Reference '%s'", location.c_str());
            return {};
        }

        resolved = json;
    }

    std::vector<std::string> locals = split_string(local, "/");

    for (std::string& name : locals) {
        find_and_replace(name, "~0", "~");
        find_and_replace(name, "~1", "/");

        if (resolved.contains(name)) {
            resolved = resolved.at(name);
        } else {
            Log(LogLevel::Error, "Failed to find swagger Reference '%s' for '%s'", name.c_str(),
                local.c_str());
            return {};
        }
    }

    if (resolved.is_object() && relative.is_object()) {
        resolved.merge_patch(relative);
    }

    return resolved;
}

nljson import_schema_example(const nljson& schema, const nljson& swagger) {
    nljson schema_value = resolve_swagger_ref(schema, swagger);

    if (schema_value.contains("example")) {
        return schema_value.at("example");
    }

    if (schema_value.contains("type")) {
        if (schema_value.at("type") == "object") {
            std::unordered_map<std::string, nljson> object_example;

            if (schema_value.contains("properties")) {
                for (auto& [key, value] : schema_value.at("properties").items()) {
                    object_example.emplace(key, import_schema_example(value, swagger));
                }
            }

            return object_example;
        }

        if (schema_value.at("type") == "array") {
            std::vector<nljson> array_example;

            if (schema_value.contains("items")) {
                array_example.emplace_back(
                    import_schema_example(schema_value.at("items"), swagger));
            }

            return array_example;
        }

        // Encode weetee variable as a json object with specific key
        std::string example_var = schema_value.at("type");
        nljson object_var = nljson::object();
        object_var.emplace(WEETEE_VARIABLE_KEY, example_var);
        return object_var;
    }

    return schema_value;
}

std::pair<Variables, Parameters> import_swagger_parameters(const nljson& parameters,
                                                           const nljson& swagger) {
    Variables vars;
    Parameters params;

    for (const auto& param : parameters) {
        nljson param_value = resolve_swagger_ref(param, swagger);

        if (!param_value.contains("name") || !param_value.contains("in")) {
            continue;
        }

        std::string name = param_value.at("name");
        std::string in = param_value.at("in");

        std::string value = "";

        if (param_value.contains("example")) {
            value = param_value.at("example");
        } else if (param_value.contains("examples")) {
            const auto& examples = param_value.at("examples");
            for (auto example = examples.begin(); example != examples.end(); example++) {
                if (example.value().contains("value")) {
                    value = example.value().at("value");
                }

                // TODO: Multiple tests for each example
                break;
            }
        } else if (param_value.contains("schema")) {
            value = unpack_variables(import_schema_example(param_value.at("schema"), swagger), 0);
            find_and_replace(value, "\n", "");
        }

        if (value.front() == '"' && value.back() == '"') {
            value = value.substr(1, value.size() - 2);
        }

        // Don't put required on this one as it won't remain required after url
        // variables resolve and you should provide a way to change it for user

        if (in == "query") {
            params.elements.push_back({
                .key = name,
                .data = {.data = value},
            });
        } else {
            vars.elements.push_back({
                .key = name,
                .data = {.data = value},
            });
        }
    }
    return {vars, params};
}

void AppState::import_swagger_paths(const nljson& paths, const nljson& swagger) {
    for (auto path = paths.begin(); path != paths.end(); path++) {
        std::string endpoint = "{url}" + path.key();

        nljson value = resolve_swagger_ref(path.value(), swagger);

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

        Parameters group_params;
        if (value.contains("parameters")) {
            value.at("parameters");

            auto [vars, params] = import_swagger_parameters(value.at("parameters"), swagger);
            new_group.variables = vars;
            group_params = params;
        }

        auto group_vars = this->get_test_variables(group_id);
        const auto& operations = value;
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

                    .request = {.parameters = group_params},
                    .response = {},

                    .cli_settings = {},
                };

                new_group.children_ids.push_back(test_id);
                this->tests.emplace(test_id, new_test);
            }
            Test& new_test = std::get<Test>(this->tests.at(test_id));

            // Variables/Parameters

            if (op.value().contains("parameters")) {
                auto [vars, params] =
                    import_swagger_parameters(op.value().at("parameters"), swagger);
                new_test.variables = vars;
                std::copy(params.elements.begin(), params.elements.end(),
                          std::back_inserter(new_test.request.parameters.elements));
            }

            test_resolve_url_variables(group_vars, &new_test);

            // Body

            if (op.value().contains("requestBody")) {
                nljson body_value = resolve_swagger_ref(op.value().at("requestBody"), swagger);

                if (body_value.contains("content")) {
                    const auto& content = body_value.at("content");
                    for (auto it = content.begin(); it != content.end(); it++) {
                        std::string content_type = it.key();
                        const auto& media_type = it.value();

                        new_test.request.body_type = request_body_type(content_type);

                        if (new_test.request.body_type == REQUEST_OTHER) {
                            new_test.request.other_content_type = content_type;
                        }

                        if (media_type.contains("example")) {
                            new_test.request.body = media_type.at("example").dump(4);

                            if (new_test.request.body_type == REQUEST_MULTIPART) {
                                request_body_convert<REQUEST_MULTIPART>(&new_test);
                            }
                        } else if (media_type.contains("schema")) {
                            new_test.request.body = unpack_variables(
                                import_schema_example(media_type.at("schema"), swagger), 4);
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

void AppState::import_swagger(const std::string& swagger_file) {
    std::ifstream in(swagger_file);
    if (!in) {
        Log(LogLevel::Error, "Failed to open file %s", swagger_file.c_str());
        return;
    }

    nljson swagger = nljson::parse(in, nullptr, false);
    if (swagger.is_discarded()) {
        return;
    }

    this->tree_view.selected_tests.clear();
    this->tests = {
        {0, root_initial},
    };

    this->saved_file = {};
    this->id_counter = 0;
    this->test_results.clear();
    this->tree_view.filtered_tests.clear();
    this->editor.open_tabs.clear();

    try {
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
            import_swagger_paths(swagger.at("paths"), swagger);
        } else {
            Log(LogLevel::Warning, "Failed to import swagger paths");
        }

        Log(LogLevel::Info, "Successfully imported swagger file '%s'", swagger_file.c_str());

        // Pushes initial undo state
        this->undo_history.reset_undo_history(this);
    } catch (std::exception& e) {
        Log(LogLevel::Error, "Failed to import swagger: %s", e.what());
    }
}

namespace swagger_export {
struct Parameter {
    std::string name;
    std::string in;
    nljson schema;
    std::optional<nljson> example;
};

void to_json(nljson& j, const Parameter& param) {
    j.emplace("name", param.name);
    j.emplace("in", param.in);
    j.emplace("required", true);
    j.emplace("schema", param.schema);
    if (param.example.has_value()) {
        j.emplace("example", param.example.value());
    }
}

struct RequestBody {
    std::string media_type;
    nljson schema;
    std::optional<nljson> example;
};

void to_json(nljson& j, const RequestBody& request_body) {
    j.emplace("content", nljson::object());
    j.at("content").emplace(request_body.media_type, nljson::object());
    j.at("content").at(request_body.media_type).emplace("schema", request_body.schema);
    if (request_body.example.has_value()) {
        j.at("content")
            .at(request_body.media_type)
            .emplace("example", request_body.example.value());
    }
}

struct Operation {
    std::string path;
    HTTPType type;
    std::vector<Parameter> parameters;
    std::optional<RequestBody> request_body;
};

nljson export_example(const std::string& value_str) {
    auto value = nljson::parse(value_str, nullptr, false);
    if (value.is_discarded()) {
        return value_str; // String
    } else {
        return value; // Any
    }
}

nljson export_schema(const nljson& example) {
    nljson result = nljson::object();

    if (example.is_object()) {
        result.emplace("type", "object");
        result.emplace("properties", nljson::object());
        for (auto& [name, value] : example.items()) {
            result.at("properties").emplace(name, export_schema(value));
        }
    } else if (example.is_array()) {
        result.emplace("type", "array");
        result.emplace("items", nljson::array());

        if (example.size() > 0) {
            result.at("items").push_back(export_schema(example.at(0)));
        } else {
            result.at("items").push_back({"type", "string"});
        }
    } else {
        result.emplace("type", example.type_name());
    }

    return result;
}
} // namespace swagger_export

void AppState::export_swagger_paths(nlohmann::json& swagger) const {
    using namespace swagger_export;
    std::unordered_map<std::string, Operation> paths;

    size_t it_id = 0, it_idx = 0;

    auto lower_http_type_label = [](HTTPType type) -> std::string {
        std::string label = HTTPTypeLabels[type];
        std::for_each(label.begin(), label.end(), [](char& c) { c = std::tolower(c); });
        return label;
    };

    while (it_id != -1 && !this->root_group()->children_ids.empty()) {
        assert(this->tests.contains(it_id));
        const NestedTest* iterated_nt = &this->tests.at(it_id);

        assert(std::holds_alternative<Group>(*iterated_nt));
        const Group* iterated_group = &std::get<Group>(*iterated_nt);

        assert(it_idx < iterated_group->children_ids.size());
        assert(this->tests.contains(iterated_group->children_ids.at(it_idx)));
        const NestedTest* it_nt = &this->tests.at(iterated_group->children_ids.at(it_idx));

        if (std::holds_alternative<Test>(*it_nt)) {
            const Test* it_test = &std::get<Test>(*it_nt);
            if (!(it_test->flags & TEST_DISABLED) && !this->parent_disabled(it_test->id)) {
                const VariablesMap& vars = this->get_test_variables(it_test->id);

                std::string path = split_endpoint(it_test->endpoint).second;

                std::string name = lower_http_type_label(it_test->type) + "_" + path;
                find_and_replace(name, "/", "_");
                find_and_replace(name, "{", "");
                find_and_replace(name, "}", "");

                if (!paths.contains(name)) {
                    Operation op = {.path = path, .type = it_test->type};

                    // Url params
                    std::vector<std::string> params = parse_url_params(path);

                    for (auto& param : params) {
                        std::optional<nljson> example = std::nullopt;

                        if (vars.contains(param)) {
                            example = export_example(vars.at(param));
                        }

                        op.parameters.push_back({
                            .name = param,
                            .in = "path",
                            .schema = export_schema(example.value_or(nljson::string_t{})),
                            .example = example,
                        });
                    }

                    // Header params
                    for (auto& header : it_test->request.headers.elements) {
                        if (header.flags & PARTIAL_DICT_ELEM_ENABLED) {
                            bool common = false;

                            for (size_t i = 0; i < ARRAY_SIZE(RequestHeadersLabels); i++) {
                                if (header.key == RequestHeadersLabels[i]) {
                                    common = true;
                                    break;
                                }
                            }

                            if (!common) {
                                nljson example =
                                    export_example(replace_variables(vars, header.data.data));

                                op.parameters.push_back({
                                    .name = header.key,
                                    .in = "header",
                                    .schema = export_schema(example),
                                    .example = example,
                                });
                            }
                        }
                    }

                    // Query params
                    for (auto& query : it_test->request.parameters.elements) {
                        if (query.flags & PARTIAL_DICT_ELEM_ENABLED) {
                            nljson example =
                                export_example(replace_variables(vars, query.data.data));

                            op.parameters.push_back({
                                .name = query.key,
                                .in = "query",
                                .schema = export_schema(example),
                                .example = example,
                            });
                        }
                    }

                    // Cookie params
                    for (auto& cookie : it_test->request.cookies.elements) {
                        if (cookie.flags & PARTIAL_DICT_ELEM_ENABLED) {
                            nljson example =
                                export_example(replace_variables(vars, cookie.data.data));

                            op.parameters.push_back({
                                .name = cookie.key,
                                .in = "cookie",
                                .schema = export_schema(example),
                                .example = example,
                            });
                        }
                    }

                    // Request body
                    if (!std::visit(EmptyVisitor(), it_test->request.body)) {
                        std::string media_type = to_string(request_content_type(&it_test->request));
                        std::optional<nljson> example = std::nullopt;
                        if (std::holds_alternative<std::string>(it_test->request.body)) {
                            example = export_example(replace_variables(
                                vars, std::get<std::string>(it_test->request.body)));
                        }

                        nljson schema_json = nljson::string_t{};
                        if (example.has_value()) {
                            schema_json = example.value();
                        }

                        nljson export_schema_json = export_schema(schema_json);

                        op.request_body = swagger_export::RequestBody{
                            .media_type = media_type,
                            .schema = export_schema_json,
                            .example = example,
                        };
                    }

                    paths.emplace(name, op);
                }
            }
        }

        iterate_over_nested_children(this, &it_id, &it_idx, -1);
    }

    swagger.emplace("paths", nljson::object());
    for (const auto& [id, op] : paths) {
        nljson operation = {};

        operation.emplace("operationId", id);
        operation.emplace("parameters", op.parameters);
        if (op.request_body.has_value()) {
            operation.emplace("requestBody", op.request_body.value());
        }

        operation.emplace("responses", nljson::parse(R"json(
        {
            "200": {
                "description": "Ok"
            }
        }
        )json"));

        swagger.at("paths").emplace(op.path, nljson::object());
        swagger.at("paths").at(op.path).emplace(lower_http_type_label(op.type), operation);
    }
}

void AppState::export_swagger_servers(nlohmann::json& swagger) const {
    VariablesMap root_vars = this->get_test_variables(0);
    if (root_vars.contains("url")) {
        nljson servers = {};
        servers.emplace_back();                             // Make a single element
        servers.back().emplace("url", root_vars.at("url")); // Add to it a url

        nljson variables = nljson::object();
        for (const auto& [key, value] : root_vars) {
            if (key == "url") {
                continue;
            }

            nljson var = {};
            var.emplace("default", value);

            variables.emplace(key, var);
            servers.back().emplace("variables", variables);
        }

        swagger.emplace("servers", servers);
    }
}

void AppState::export_swagger(const std::string& swagger_file) const {
    std::ofstream out(swagger_file);
    if (!out) {
        Log(LogLevel::Error, "Failed to open file %s", swagger_file.c_str());
        return;
    }

    try {
        nljson swagger = {};
        swagger.emplace("openapi", "3.0.0"); // Version

        {
            nljson info = {};
            info.emplace("title", this->root_group()->name);
            info.emplace("version", "0.1.0");

            swagger.emplace("info", info);
        }

        this->export_swagger_servers(swagger);
        this->export_swagger_paths(swagger);

        out << swagger.dump(2);
        Log(LogLevel::Info, "Successfully exported swagger to '%s'", swagger_file.c_str());
    } catch (nljson::type_error& te) {
        Log(LogLevel::Error, "Failed to export swagger: %s", te.what());
    } catch (std::exception& e) {
        Log(LogLevel::Error, "Failed to export swagger: %s", e.what());
    }
}

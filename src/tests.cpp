#include "tests.hpp"
#include "algorithm"
#include "fstream"
#include "http.hpp"
#include "partial_dict.hpp"

bool test_comp(const std::unordered_map<size_t, NestedTest>& tests, size_t a_id, size_t b_id) {
    assert(tests.contains(a_id));
    assert(tests.contains(b_id));

    const NestedTest& a = tests.at(a_id);
    const NestedTest& b = tests.at(b_id);

    bool group_a = std::holds_alternative<Group>(a);
    bool group_b = std::holds_alternative<Group>(b);

    if (group_a != group_b) {
        return group_a > group_b;
    }

    std::string label_a = std::visit(LabelVisitor(), a);
    std::string label_b = std::visit(LabelVisitor(), b);

    return label_a > label_b;
}

MultiPartBody request_multipart_convert_json(const nlohmann::json& json) {
    if (json.is_discarded()) {
        return {};
    }

    if (!json.is_object()) {
        return {};
    }

    MultiPartBody to_replace = {};

    for (const auto& [key, value] : json.items()) {
        MultiPartBodyElement new_elem;
        if (value.is_string()) {
             MultiPartBodyElementData new_data = {
                .type = MPBD_TEXT,
                .data = value.template get<std::string>(),
            };

            new_elem = MultiPartBodyElement{
                .key = key,
                .data = new_data,
            };
        } else if (value.is_array()) {
            std::vector<std::string> files;

            for (const auto& file : value.items()) {
                if (file.value().is_string()) {
                    files.push_back(file.value());
                } else {
                    return {}; // Not MultiPartBody
                }
            }

            MultiPartBodyElementData new_data = {
                .type = MPBD_FILES,
                .data = files,
            };

            new_elem = MultiPartBodyElement { 
                .key = key,
                .data = new_data,
            };
        } else {
            return {}; // Not MultiPartBody
        }

        new_elem.data.resolve_content_type();
        to_replace.elements.push_back(new_elem);
    }

    return to_replace;
}

std::string Test::label() const { return this->endpoint + "##" + to_string(this->id); }

std::string Group::label() const { return this->name + "##" + to_string(this->id); }

RequestBodyType request_body_type(const std::string& str) {
    if (str == "application/json") {
        return REQUEST_JSON;
    } else if (str == "text/plain") {
        return REQUEST_PLAIN;
    } else if (str == "multipart/form-data") {
        return REQUEST_MULTIPART;
    }

    return REQUEST_OTHER;
}

ContentType request_content_type(const Request* request) {
    switch (request->body_type) {
    case REQUEST_JSON:
        return {.type = "application", .name = "json"};
    case REQUEST_MULTIPART:
        return {.type = "multipart", .name = "form-data"};
    case REQUEST_PLAIN:
        return {.type = "text", .name = "plain"};
    case REQUEST_OTHER:
        return parse_content_type(request->other_content_type);
    }
    assert(false && "Unreachable");
    return {};
}

void test_resolve_url_variables(const VariablesMap& parent_vars, Test* test) {
    std::vector<std::string> param_names = parse_url_params(test->endpoint);

    // Add new required variables
    for (const std::string& name : param_names) {
        if (parent_vars.contains(name)) {
            continue;
        }

        auto param = std::find_if(test->variables.elements.begin(), test->variables.elements.end(),
                                  [&name](const auto& elem) { return elem.key == name; });

        if (param == test->variables.elements.end()) {
            test->variables.elements.push_back({
                .flags = PARTIAL_DICT_ELEM_ENABLED | PARTIAL_DICT_ELEM_REQUIRED,
                .key = name,
                .data = {},
                .cfs = {},
            });
        } else {
            param->flags = PARTIAL_DICT_ELEM_ENABLED | PARTIAL_DICT_ELEM_REQUIRED;
        }
    }

    // Remove required from old ones
    std::for_each(test->variables.elements.begin(), test->variables.elements.end(),
                  [&param_names, &parent_vars](auto& elem) {
                      auto param = std::find_if(
                          param_names.begin(), param_names.end(),
                          [&elem](const std::string& name) { return elem.key == name; });

                      if (param == param_names.end() || parent_vars.contains(elem.key)) {
                          elem.flags &= ~PARTIAL_DICT_ELEM_REQUIRED;
                      }
                  });
}

httplib::Headers
request_headers(const VariablesMap& vars, const Test* test,
                const std::unordered_map<std::string, std::string>* overload_cookies) {
    httplib::Headers result;

    for (const auto& header : test->request.headers.elements) {
        if (!(header.flags & PARTIAL_DICT_ELEM_ENABLED)) {
            continue;
        }
        result.emplace(replace_variables(vars, header.key),
                       replace_variables(vars, header.data.data));
    }

    if (overload_cookies != nullptr) {
        for (const auto& [key, value] : *overload_cookies) {
            result.emplace("Cookie", key + "=" + value);
        }
    } else {
        for (const auto& cookie : test->request.cookies.elements) {
            if (!(cookie.flags & PARTIAL_DICT_ELEM_ENABLED)) {
                continue;
            }
            result.emplace("Cookie", replace_variables(vars, cookie.key) + "=" +
                                         replace_variables(vars, cookie.data.data));
        }
    }

    return result;
}

httplib::Params request_params(const VariablesMap& vars, const Test* test) {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!(param.flags & PARTIAL_DICT_ELEM_ENABLED)) {
            continue;
        }
        result.emplace(replace_variables(vars, param.key),
                       replace_variables(vars, param.data.data));
    };

    return result;
}

RequestBodyResult request_body(const VariablesMap& vars, const Test* test) {
    if (std::holds_alternative<std::string>(test->request.body)) {
        return {
            .content_type = to_string(request_content_type(&test->request)),
            .body = replace_variables(vars, std::get<std::string>(test->request.body)),
        };
    }

    // multi part body
    assert(std::holds_alternative<MultiPartBody>(test->request.body));
    const auto& mp = std::get<MultiPartBody>(test->request.body);
    httplib::MultipartFormDataItems form;

    for (const auto& elem : mp.elements) {
        switch (elem.data.type) {
        case MPBD_FILES: {
            assert(std::holds_alternative<std::vector<std::string>>(elem.data.data));
            auto& files = std::get<std::vector<std::string>>(elem.data.data);

            std::vector<std::string> types =
                split_string(replace_variables(vars, elem.data.content_type), ",");

            for (size_t file_idx = 0; file_idx < files.size(); file_idx += 1) {
                const auto& file = files.at(file_idx);

                std::string content_type = "text/plain";
                if (file_idx < types.size()) { // types could be smaller
                    content_type = types[file_idx];
                }

                std::ifstream in(file);
                if (in) {
                    std::string file_content;
                    httplib::detail::read_file(file, file_content);
                    httplib::MultipartFormData data = {
                        .name = elem.key,
                        .content = file_content,
                        .filename = get_filename(file),
                        .content_type = content_type,
                    };
                    form.push_back(data);
                }
            }
        } break;
        case MPBD_TEXT: {
            assert(std::holds_alternative<std::string>(elem.data.data));
            auto& str = std::get<std::string>(elem.data.data);

            httplib::MultipartFormData data = {
                .name = elem.key,
                .content = replace_variables(vars, str),
                .filename = "",
                .content_type = replace_variables(vars, elem.data.content_type),
            };
            form.push_back(data);
        } break;
        }
    }

    const auto& boundary = httplib::detail::make_multipart_data_boundary();
    const auto& content_type =
        httplib::detail::serialize_multipart_formdata_get_content_type(boundary);
    const auto& body = httplib::detail::serialize_multipart_formdata(form, boundary);

    return {
        .content_type = content_type,
        .body = body,
    };
}

ContentType response_content_type(const Response* response) {
    switch (response->body_type) {
    case RESPONSE_ANY:
        return {.type = "", .name = ""};
    case RESPONSE_JSON:
        return {.type = "application", .name = "json"};
    case RESPONSE_HTML:
        return {.type = "text", .name = "html"};
    case RESPONSE_PLAIN:
        return {.type = "text", .name = "plain"};
    case RESPONSE_OTHER:
        return parse_content_type(response->other_content_type);
    }

    assert(false && "Unreachable");
    return {};
}

httplib::Headers response_headers(const VariablesMap& vars, const Test* test) {
    httplib::Headers result;

    for (const auto& header : test->response.headers.elements) {
        if (!(header.flags & PARTIAL_DICT_ELEM_ENABLED)) {
            continue;
        }
        result.emplace(header.key, replace_variables(vars, header.data.data));
    }

    for (const auto& cookie : test->response.cookies.elements) {
        if (!(cookie.flags & PARTIAL_DICT_ELEM_ENABLED)) {
            continue;
        }
        result.emplace("Set-Cookie", replace_variables(vars, cookie.key) + "=" +
                                         replace_variables(vars, cookie.data.data));
    }

    return result;
}

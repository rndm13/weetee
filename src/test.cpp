#include "test.hpp"
#include "fstream"
#include "http.hpp"
#include "imgui.h"
#include "partial_dict.hpp"
#include <algorithm>

#define CHECKBOX_FLAG(flags, changed, flag_name, flag_label)                                       \
    do {                                                                                           \
        bool flag = (flags) & (flag_name);                                                         \
        if (ImGui::Checkbox((flag_label), &flag)) {                                                \
            changed = true;                                                                        \
            if (flag) {                                                                            \
                (flags) |= (flag_name);                                                            \
            } else {                                                                               \
                (flags) &= ~(flag_name);                                                           \
            }                                                                                      \
        }                                                                                          \
    } while (0);

#define COMBO_VARIANT(label, variant, changed, variant_labels, variant_type)                       \
    do {                                                                                           \
        size_t type = (variant).index();                                                           \
        if (ImGui::BeginCombo((label), (variant_labels)[type])) {                                  \
            for (size_t i = 0; i < ARRAY_SIZE((variant_labels)); i++) {                            \
                if (ImGui::Selectable((variant_labels)[i], i == type)) {                           \
                    (changed) = true;                                                              \
                    type = static_cast<size_t>(i);                                                 \
                    (variant) = variant_from_index<variant_type>(type);                            \
                }                                                                                  \
            }                                                                                      \
            ImGui::EndCombo();                                                                     \
        }                                                                                          \
    } while (0);

bool show_auth(std::string label, AuthVariant* auth) noexcept {
    bool changed = false;
    COMBO_VARIANT(label.c_str(), *auth, changed, AuthTypeLabels, AuthVariant);
    std::visit(overloaded{
                   [&changed, &label](std::monostate) {},
                   [&changed, &label](AuthBasic& basic) {
                       changed |= ImGui::InputText((label + " Name").c_str(), &basic.name);
                       changed |= ImGui::InputText((label + " Password").c_str(), &basic.password,
                                                   ImGuiInputTextFlags_Password);
                   },
                   [&changed, &label](AuthBearerToken& token) {
                       changed |= ImGui::InputText((label + " Token").c_str(), &token.token);
                   },
               },
               *auth);

    return changed;
}

bool show_client_settings(ClientSettings* set) noexcept {
    assert(set);

    bool changed = false;

    CHECKBOX_FLAG(set->flags, changed, CLIENT_DYNAMIC, "Dynamic Testing");
    if (set->flags & CLIENT_DYNAMIC) {
        ImGui::SameLine();
        CHECKBOX_FLAG(set->flags, changed, CLIENT_KEEP_ALIVE, "Keep Alive Connection");
    }

    CHECKBOX_FLAG(set->flags, changed, CLIENT_FOLLOW_REDIRECTS, "Follow Redirects");
    changed |= show_auth("Authentication", &set->auth);

    CHECKBOX_FLAG(set->flags, changed, CLIENT_PROXY, "Set proxy");
    if (set->flags & CLIENT_PROXY) {
        changed |= ImGui::InputText("Proxy Host", &set->proxy_host);
        changed |= ImGui::InputInt("Proxy Port", &set->proxy_port);
        changed |= show_auth("Proxy Authentication", &set->proxy_auth);
    }

    return changed;
}

#undef CHECKBOX_FLAG
#undef COMBO_MASK

std::vector<std::pair<size_t, size_t>> encapsulation_ranges(std::string str, char begin,
                                                            char end) noexcept {
    std::vector<std::pair<size_t, size_t>> result;

    size_t index = 0;
    do {
        size_t left_brace = str.find(begin, index);
        size_t right_brace = str.find(end, left_brace);
        if (left_brace == std::string::npos || right_brace == std::string::npos) {
            break;
        }

        result.emplace_back(left_brace, right_brace - left_brace);
        index = right_brace + 1;
    } while (index < str.size());

    return result;
}

std::string request_endpoint(const Variables* vars, const Test* test) noexcept {
    assert(test);

    std::string result = replace_variables(vars, test->endpoint);

    if (test->request.url_parameters.empty()) {
        return result;
    }

    std::vector<std::pair<size_t, size_t>> params_idx = encapsulation_ranges(result, '{', '}');
    std::for_each(
        params_idx.rbegin(), params_idx.rend(),
        [&result, vars, &params = test->request.url_parameters](std::pair<size_t, size_t> range) {
            auto [begin, size] = range;

            std::string name = result.substr(begin + 1, size - 1);

            auto found = std::find_if(
                params.elements.begin(), params.elements.end(),
                [&name](const auto& elem) { return elem.enabled && elem.key == name; });

            if (found != params.elements.end()) {
                result.replace(begin, size + 1, replace_variables(vars, found->data.data));
            }
        });

    return result;
}

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
void Request::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->body_type);
    save->save(this->body);
    save->save(this->cookies);
    save->save(this->url_parameters);
    save->save(this->parameters);
    save->save(this->headers);
}

bool Request::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->body_type)) {
        return false;
    }
    if (!save->can_load(this->body)) {
        return false;
    }
    if (!save->can_load(this->cookies)) {
        return false;
    }
    if (!save->can_load(this->url_parameters)) {
        return false;
    }
    if (!save->can_load(this->parameters)) {
        return false;
    }
    if (!save->can_load(this->headers)) {
        return false;
    }

    return true;
}

void Request::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->body_type);
    save->load(this->body);
    save->load(this->cookies);
    save->load(this->url_parameters);
    save->load(this->parameters);
    save->load(this->headers);
}

void Response::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->status);
    save->save(this->body_type);
    save->save(this->body);
    save->save(this->cookies);
    save->save(this->headers);
}

bool Response::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->status)) {
        return false;
    }
    if (!save->can_load(this->body_type)) {
        return false;
    }
    if (!save->can_load(this->body)) {
        return false;
    }
    if (!save->can_load(this->cookies)) {
        return false;
    }
    if (!save->can_load(this->headers)) {
        return false;
    }
    return true;
}

void Response::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->status);
    save->load(this->body_type);
    save->load(this->body);
    save->load(this->cookies);
    save->load(this->headers);
}

void AuthBasic::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->name);
    save->save(this->password);
}

bool AuthBasic::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->name)) {
        return false;
    }

    if (!save->can_load(this->password)) {
        return false;
    }

    return true;
}

void AuthBasic::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->name);
    save->load(this->password);
}

void AuthBearerToken::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->token);
}

bool AuthBearerToken::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->token)) {
        return false;
    }

    return true;
}

void AuthBearerToken::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->token);
}

void ClientSettings::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->flags);
    // NOTE: can disable save when CLIENT_PROXY isn't set
    save->save(this->auth);
    save->save(this->proxy_host);
    save->save(this->proxy_port);
    save->save(this->proxy_auth);
}

bool ClientSettings::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->flags)) {
        return false;
    }

    if (!save->can_load(this->auth)) {
        return false;
    }

    if (!save->can_load(this->proxy_host)) {
        return false;
    }

    if (!save->can_load(this->proxy_port)) {
        return false;
    }

    if (!save->can_load(this->proxy_auth)) {
        return false;
    }

    return true;
}

void ClientSettings::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->flags);
    save->load(this->auth);
    save->load(this->proxy_host);
    save->load(this->proxy_port);
    save->load(this->proxy_auth);
}

std::string Test::label() const noexcept { return this->endpoint + "##" + to_string(this->id); }

void Test::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->id);
    save->save(this->parent_id);
    save->save(this->type);
    save->save(this->flags);
    save->save(this->endpoint);
    save->save(this->request);
    save->save(this->response);
    save->save(this->cli_settings);
}

bool Test::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->id)) {
        return false;
    }
    if (!save->can_load(this->parent_id)) {
        return false;
    }
    if (!save->can_load(this->type)) {
        return false;
    }
    if (!save->can_load(this->flags)) {
        return false;
    }
    if (!save->can_load(this->endpoint)) {
        return false;
    }
    if (!save->can_load(this->request)) {
        return false;
    }
    if (!save->can_load(this->response)) {
        return false;
    }
    if (!save->can_load(this->cli_settings)) {
        return false;
    }
    return true;
}

void Test::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->id);
    save->load(this->parent_id);
    save->load(this->type);
    save->load(this->flags);
    save->load(this->endpoint);
    save->load(this->request);
    save->load(this->response);
    save->load(this->cli_settings);
}

std::string Group::label() const noexcept { return this->name + "##" + to_string(this->id); }

void Group::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->id);
    save->save(this->parent_id);
    save->save(this->flags);
    save->save(this->name);
    save->save(this->children_ids);
    save->save(this->cli_settings);
    save->save(this->variables);
}

bool Group::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->id)) {
        return false;
    }
    if (!save->can_load(this->parent_id)) {
        return false;
    }
    if (!save->can_load(this->flags)) {
        return false;
    }
    if (!save->can_load(this->name)) {
        return false;
    }
    if (!save->can_load(this->children_ids)) {
        return false;
    }
    if (!save->can_load(this->cli_settings)) {
        return false;
    }
    if (!save->can_load(this->variables)) {
        return false;
    }

    return true;
}

void Group::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->id);
    save->load(this->parent_id);
    save->load(this->flags);
    save->load(this->name);
    save->load(this->children_ids);
    save->load(this->cli_settings);
    save->load(this->variables);
}

ContentType request_content_type(const Request* request) noexcept {
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

httplib::Headers request_headers(const Variables* vars, const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->request.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(replace_variables(vars, header.key),
                       replace_variables(vars, header.data.data));
    }

    for (const auto& cookie : test->request.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Cookie", replace_variables(vars, cookie.key) + "=" +
                                     replace_variables(vars, cookie.data.data));
    }

    return result;
}

httplib::Params request_params(const Variables* vars, const Test* test) noexcept {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!param.enabled) {
            continue;
        }
        result.emplace(replace_variables(vars, param.key),
                       replace_variables(vars, param.data.data));
    };

    return result;
}

RequestBodyResult request_body(const Variables* vars, const Test* test) noexcept {
    if (std::holds_alternative<std::string>(test->request.body)) {
        return {
            .content_type = to_string(request_content_type(&test->request)),
            .body = replace_variables(vars, std::get<std::string>(test->request.body)),
        };
    }

    // multi part body
    std::holds_alternative<MultiPartBody>(test->request.body);
    const auto& mp = std::get<MultiPartBody>(test->request.body);
    httplib::MultipartFormDataItems form;

    for (const auto& elem : mp.elements) {
        std::visit(overloaded{
                       [&form, &elem, vars](const std::string& str) {
                           httplib::MultipartFormData data = {
                               .name = elem.key,
                               .content = replace_variables(vars, str),
                               .filename = "",
                               .content_type = replace_variables(vars, elem.data.content_type),
                           };
                           form.push_back(data);
                       },
                       [&form, &elem, vars](const std::vector<std::string>& files) {
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
                                       .filename = file_name(file),
                                       .content_type = content_type,
                                   };
                                   form.push_back(data);
                               }
                           }
                       },
                   },
                   elem.data.data);
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

ContentType response_content_type(const Response* response) noexcept {
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

httplib::Headers response_headers(const Variables* vars, const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->response.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, replace_variables(vars, header.data.data));
    }

    for (const auto& cookie : test->response.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Set-Cookie", replace_variables(vars, cookie.key) + "=" +
                                         replace_variables(vars, cookie.data.data));
    }

    return result;
}

std::string replace_variables(const Variables* vars, const std::string& target,
                              size_t recursion) noexcept {
    std::string result = target;
    if (recursion > REPLACE_VARIABLES_MAX_NEST) {
        return result;
    }
    std::vector<std::pair<size_t, size_t>> params_idx = encapsulation_ranges(result, '<', '>');
    std::for_each(params_idx.rbegin(), params_idx.rend(),
                  [&result, vars, recursion](std::pair<size_t, size_t> range) {
                      auto [begin, size] = range;

                      std::string name = result.substr(begin + 1, size - 1);

                      auto found = std::find_if(
                          vars->elements.begin(), vars->elements.end(),
                          [&name](const auto& elem) { return elem.enabled && elem.key == name; });

                      if (found != vars->elements.end()) {
                          result.replace(begin, size + 1, replace_variables(vars, found->data.data, recursion + 1));
                      }
                  });

    return result;
}

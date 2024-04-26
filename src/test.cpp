#include "test.hpp"
#include "fstream"
#include "imgui.h"

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

std::string request_endpoint(const Test* test) noexcept {
    assert(test);

    if (test->request.url_parameters.empty()) {
        return test->endpoint;
    }

    std::string result;

    size_t index = 0;
    do {
        size_t left_brace = test->endpoint.find("{", index);
        if (left_brace == std::string::npos) {
            result += test->endpoint.substr(index);
            break;
        }

        size_t right_brace = test->endpoint.find("}", left_brace);
        if (right_brace == std::string::npos) {
            result = "Unmatched left brace";
            break;
        }

        result += test->endpoint.substr(index, left_brace - index);
        std::string name = test->endpoint.substr(left_brace + 1, right_brace - (left_brace + 1));

        auto found = std::find_if(test->request.url_parameters.elements.begin(),
                                  test->request.url_parameters.elements.end(),
                                  [&name](const auto& elem) { return elem.key == name; });
        if (found->data.data.empty()) {
            result += "<empty>";
        } else {
            result += found->data.data;
        }

        index = right_brace + 1;
    } while (index < test->endpoint.size());

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
}

httplib::Headers request_headers(const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->request.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, header.data.data);
    }

    for (const auto& cookie : test->request.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Cookie", cookie.key + "=" + cookie.data.data);
    }

    return result;
}

ContentType request_content_type(RequestBodyType type) noexcept {
    switch (type) {
    case REQUEST_JSON:
        return {.type = "application", .name = "json"};
    case REQUEST_MULTIPART:
        return {.type = "multipart", .name = "form-data"};
    case REQUEST_PLAIN:
        return {.type = "text", .name = "plain"};
    }
    assert(false && "Unreachable");
    return {};
}

httplib::Params request_params(const Test* test) noexcept {
    httplib::Params result;

    for (const auto& param : test->request.parameters.elements) {
        if (!param.enabled) {
            continue;
        }
        result.emplace(param.key, param.data.data);
    };

    return result;
}

RequestBodyResult request_body(const Test* test) noexcept {
    if (std::holds_alternative<std::string>(test->request.body)) {
        return {
            .content_type = to_string(request_content_type(test->request.body_type)),
            .body = std::get<std::string>(test->request.body),
        };
    }

    // multi part body
    std::holds_alternative<MultiPartBody>(test->request.body);
    const auto& mp = std::get<MultiPartBody>(test->request.body);
    httplib::MultipartFormDataItems form;

    for (const auto& elem : mp.elements) {
        std::visit(overloaded{
                       [&form, &elem](const std::string& str) {
                           httplib::MultipartFormData data = {
                               .name = elem.key,
                               .content = str,
                               .filename = "",
                               .content_type = elem.data.content_type,
                           };
                           form.push_back(data);
                       },
                       [&form, &elem](const std::vector<std::string>& files) {
                           std::vector<std::string> types =
                               split_string(elem.data.content_type, ",");

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

httplib::Headers response_headers(const Test* test) noexcept {
    httplib::Headers result;

    for (const auto& header : test->response.headers.elements) {
        if (!header.enabled) {
            continue;
        }
        result.emplace(header.key, header.data.data);
    }

    for (const auto& cookie : test->response.cookies.elements) {
        if (!cookie.enabled) {
            continue;
        }
        result.emplace("Set-Cookie", cookie.key + "=" + cookie.data.data);
    }

    return result;
}

ContentType response_content_type(ResponseBodyType type) noexcept {
    switch (type) {
    case RESPONSE_JSON:
        return {.type = "application", .name = "json"};
    case RESPONSE_HTML:
        return {.type = "text", .name = "html"};
    case RESPONSE_PLAIN:
        return {.type = "text", .name = "plain"};
    }
    assert(false && "Unreachable");
    return {};
}

#include "test.hpp"

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

bool show_client_settings(ClientSettings* set) noexcept {
    assert(set);

    bool changed = false;

    CHECKBOX_FLAG(set->flags, changed, CLIENT_DYNAMIC, "Dynamic Testing");
    if (set->flags & CLIENT_DYNAMIC) {
        ImGui::SameLine();
        CHECKBOX_FLAG(set->flags, changed, CLIENT_KEEP_ALIVE, "Keep Alive Connection");
    }

    CHECKBOX_FLAG(set->flags, changed, CLIENT_FOLLOW_REDIRECTS, "Follow Redirects");
    return changed;
}
#undef CHECKBOX_FLAG

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

void ClientSettings::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->flags);
}

bool ClientSettings::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->flags)) {
        return false;
    }

    return true;
}

void ClientSettings::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->flags);
}

std::string Test::label() const noexcept {
    return this->endpoint + "##" + to_string(this->id);
}

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

    std::string Group::label() const noexcept {
        return this->name + "##" + to_string(this->id);
    }

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


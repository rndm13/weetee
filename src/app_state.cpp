#include "app_state.hpp"

#include "algorithm"
#include "fstream"
#include "hello_imgui/hello_imgui_logger.h"
#include "http.hpp"
#include "imgui_stdlib.h"
#include "iterator"
#include "partial_dict.hpp"
#include "test.hpp"
#include "utility"
#include "utils.hpp"

const Group AppState::root_initial = Group{
    .parent_id = static_cast<size_t>(-1),
    .id = 0,
    .flags = GROUP_NONE,
    .name = "root",
    .cli_settings = ClientSettings{},
    .children_ids = {},
    .variables = {},
};

Group* AppState::root_group() noexcept {
    assert(this->tests.contains(0));
    assert(std::holds_alternative<Group>(this->tests[0]));

    return &std::get<Group>(this->tests[0]);
}

const Group* AppState::root_group() const noexcept {
    assert(this->tests.contains(0));
    assert(std::holds_alternative<Group>(this->tests.at(0)));

    return &std::get<Group>(this->tests.at(0));
}

bool AppState::is_running_tests() const noexcept {
    for (const auto& [id, result] : this->test_results) {
        if (result.running.load()) {
            return true;
        }
    }

    return false;
}

void AppState::save(SaveState* save) const noexcept {
    assert(save);

    // this save is obviously going to be pretty big so reserve instantly for speed
    save->original_buffer.reserve(4096);

    save->save(this->id_counter);
    save->save(this->tests);
}

bool AppState::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->id_counter)) {
        return false;
    }
    if (!save->can_load(this->tests)) {
        return false;
    }

    return true;
}

void AppState::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->id_counter);
    save->load(this->tests);
}

void AppState::editor_open_tab(size_t id) noexcept {
    assert(this->tests.contains(id));

    this->runner_params->dockingParams.dockableWindowOfName("Editor")->focusWindowAtNextFrame =
        true;
    if (this->opened_editor_tabs.contains(id)) {
        this->opened_editor_tabs[id].just_opened = true;
    } else {
        this->opened_editor_tabs[id] = EditorTab{
            .original_idx = id,
            .name = std::visit(LabelVisitor(), this->tests[id]),
        };
    }
}

void AppState::focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests) noexcept {
    assert(old_tests);

    for (auto& [id, test] : this->tests) {
        if (old_tests->contains(id) && !nested_test_eq(&test, &old_tests->at(id))) {
            this->editor_open_tab(id);
        } else {
            if (std::holds_alternative<Test>(test)) {
                this->editor_open_tab(id);
            }
        }
    }
}

void AppState::post_open() noexcept {
    this->opened_editor_tabs.clear();
    this->selected_tests.clear();
    this->undo_history.reset_undo_history(this);
}

void AppState::post_undo() noexcept {
    for (auto it = this->opened_editor_tabs.begin(); it != this->opened_editor_tabs.end();) {
        if (!this->tests.contains(it->first)) {
            it = this->opened_editor_tabs.erase(it);
        } else {
            ++it;
        }
    }

    // remove missing tests from selected
    for (auto it = this->selected_tests.begin(); it != this->selected_tests.end();) {
        if (!this->tests.contains(*it)) {
            it = this->selected_tests.erase(it);
        } else {
            ++it;
        }
    }

    // add newly selected if parent is selected
    for (auto& [id, nt] : this->tests) {
        if (this->parent_selected(id) && !this->selected_tests.contains(id)) {
            this->select_with_children(std::visit(ParentIDVisitor(), nt));
        }
    }
}

void AppState::undo() noexcept {
    auto old_tests = this->tests;
    this->undo_history.undo(this);
    this->focus_diff_tests(&old_tests);
    this->post_undo();
}

void AppState::redo() noexcept {
    auto old_tests = this->tests;
    this->undo_history.redo(this);
    this->focus_diff_tests(&old_tests);
    this->post_undo();
}

VariablesMap AppState::get_test_variables(size_t id) const noexcept {
    VariablesMap result = {};

    while (this->tests.contains(id)) {
        const auto& test = this->tests.at(id);
        const Variables& vars_input = std::visit(VariablesVisitor(), test);
        std::for_each(
            vars_input.elements.begin(), vars_input.elements.end(),
            [&result](const VariablesElement& elem) {
                if (elem.flags & PARTIAL_DICT_ELEM_ENABLED && !result.contains(elem.key)) {

                    if (!elem.data.separator.has_value()) {
                        result.emplace(elem.key, elem.data.data);
                    } else {
                        std::vector<std::string> possible_values =
                            split_string(elem.data.data, std::string{elem.data.separator.value()});
                        result.emplace(elem.key,
                                       possible_values.at(rand() % possible_values.size()));
                    }
                }
            });

        id = std::visit(ParentIDVisitor(), test);
    }

    return result;
}

bool AppState::parent_disabled(size_t id) noexcept {
    // OPTIM: maybe add some cache for every test that clears every frame?
    // if performance becomes a problem
    assert(this->tests.contains(id));
    id = std::visit(ParentIDVisitor(), this->tests.at(id));

    while (id != -1ull) {
        if (!this->tests.contains(id)) {
            break;
        }

        assert(this->tests.contains(id));
        NestedTest* nt = &this->tests.at(id);

        assert(std::holds_alternative<Group>(*nt));
        const Group& group = std::get<Group>(*nt);
        if (group.flags & GROUP_DISABLED) {
            return true;
        }

        id = group.parent_id;
    }
    return false;
}

bool AppState::parent_selected(size_t id) const noexcept {
    assert(this->tests.contains(id));

    return this->selected_tests.contains(std::visit(ParentIDVisitor(), this->tests.at(id)));
}

ClientSettings AppState::get_cli_settings(size_t id) const noexcept {
    assert(this->tests.contains(id));

    while (id != -1ull) {
        const auto& test = this->tests.at(id);
        std::optional<ClientSettings> cli = std::visit(ClientSettingsVisitor(), test);

        if (cli.has_value()) {
            return cli.value();
        }

        id = std::visit(ParentIDVisitor(), test);
    }

    assert(false && "root doesn't have client settings");
    return {};
}

std::vector<size_t> AppState::select_top_layer() noexcept {
    std::vector<size_t> result;
    for (auto sel_id : this->selected_tests) {
        if (!this->parent_selected(sel_id)) {
            result.push_back(sel_id);
        }
    }

    return result;
}

AppState::SelectAnalysisResult AppState::select_analysis() const noexcept {
    SelectAnalysisResult result;
    result.top_selected_count = this->selected_tests.size();

    auto check_parent = [&result](size_t id) {
        if (!result.same_parent || result.selected_root) {
            return;
        }

        if (result.parent_id == -1ull) {
            result.parent_id = id;
        } else if (result.parent_id != id) {
            result.same_parent = false;
        }
    };

    for (auto test_id : this->selected_tests) {
        if (this->parent_selected(test_id)) {
            result.top_selected_count--;
            continue;
        }

        assert(this->tests.contains(test_id));
        const auto* selected = &this->tests.at(test_id);

        switch (selected->index()) {
        case TEST_VARIANT: {
            assert(std::holds_alternative<Test>(*selected));
            auto& selected_test = std::get<Test>(*selected);

            result.test = true;

            check_parent(selected_test.parent_id);
        } break;
        case GROUP_VARIANT: {
            assert(std::holds_alternative<Group>(*selected));
            auto& selected_group = std::get<Group>(*selected);

            result.group = true;
            result.selected_root |= selected_group.id == 0;

            check_parent(selected_group.parent_id);
        } break;
        }
    }

    return result;
}

void AppState::move_children_up(Group* group) noexcept {
    assert(group);
    assert(group->id != 0); // not root

    auto& parent = this->tests[group->parent_id];
    assert(std::holds_alternative<Group>(parent));
    auto& parent_group = std::get<Group>(parent);

    for (auto child_id : group->children_ids) {
        assert(this->tests.contains(child_id));
        auto& child = this->tests.at(child_id);

        std::visit(SetParentIDVisitor{parent_group.id}, child);
        parent_group.children_ids.push_back(child_id);
    }

    group->children_ids.clear();
}

void AppState::delete_children(const Group* group) noexcept {
    assert(group);
    std::vector<size_t> to_delete = group->children_ids;
    // delete_test also removes it from parent->children_idx
    // when iterating over it it creates unexpected behaviour

    for (auto child_id : to_delete) {
        this->delete_test(child_id);
    }

    assert(group->children_ids.size() <= 0); // no remaining children
}

void AppState::delete_test(size_t id) noexcept {
    assert(this->tests.contains(id));
    NestedTest* test = &this->tests[id];

    size_t parent_id = std::visit(ParentIDVisitor(), *test);

    if (std::holds_alternative<Group>(*test)) {
        auto& group = std::get<Group>(*test);
        this->delete_children(&group);
    }

    // remove it's id from parents child id list
    assert(this->tests.contains(parent_id));
    assert(std::holds_alternative<Group>(this->tests[parent_id]));
    auto& parent = std::get<Group>(this->tests[parent_id]);

    size_t count = std::erase(parent.children_ids, id);
    assert(count == 1);

    // remove from tests
    this->tests.erase(id);

    this->opened_editor_tabs.erase(id);
    this->selected_tests.erase(id);
}

void AppState::delete_selected() noexcept {
    for (auto test_id : this->select_top_layer()) {
        this->delete_test(test_id);
    }
}

void AppState::group_selected(size_t common_parent_id) noexcept {
    assert(this->tests.contains(common_parent_id));
    auto* parent_test = &this->tests[common_parent_id];
    assert(std::holds_alternative<Group>(*parent_test));
    auto& parent_group = std::get<Group>(*parent_test);

    // remove selected from old parent
    size_t count = std::erase_if(parent_group.children_ids,
                                 [&sel_tests = this->selected_tests](size_t child_id) {
                                     return sel_tests.contains(child_id);
                                 });
    assert(count >= 1);

    auto id = ++this->id_counter;
    auto new_group = Group{
        .parent_id = parent_group.id,
        .id = id,
        .flags = GROUP_OPEN,
        .name = "New group",
        .cli_settings = {},
        .children_ids = {},
        .variables = {},
    };

    // Copy selected to new group
    std::copy_if(this->selected_tests.begin(), this->selected_tests.end(),
                 std::back_inserter(new_group.children_ids), [this](size_t selected_id) {
                     assert(this->tests.contains(selected_id));
                     return !this->parent_selected(selected_id);
                 });

    // Set selected parent id to new group's id
    std::for_each(this->selected_tests.begin(), this->selected_tests.end(),
                  [this, id](size_t test_id) {
                      assert(this->tests.contains(test_id));

                      if (!this->parent_selected(test_id)) {
                          std::visit(SetParentIDVisitor{id}, this->tests.at(test_id));
                      }
                  });

    // Add new group to original parent's children
    parent_group.children_ids.push_back(id);

    this->tests.emplace(id, new_group);
}

void AppState::copy() noexcept {
    std::unordered_map<size_t, NestedTest> to_copy = {};

    to_copy.reserve(this->selected_tests.size());
    for (auto sel_id : this->selected_tests) {
        assert(this->tests.contains(sel_id));

        to_copy.emplace(sel_id, this->tests.at(sel_id));
    }

    this->clipboard = {};
    this->clipboard.save(to_copy);
    this->clipboard.finish_save();
}

void AppState::cut() noexcept {
    this->copy();
    this->delete_selected();
}

void AppState::paste(Group* group) noexcept {
    std::unordered_map<size_t, NestedTest> to_paste = {};
    this->clipboard.load(to_paste);
    this->clipboard.reset_load();

    // Increments used ids
    // for tests updates id, parent children_idx (if parent present)
    // for groups should also update all children parent_id

    for (size_t iterations = 0; iterations < to_paste.size(); iterations++) {
        auto it = std::find_if(to_paste.begin(), to_paste.end(),
                               [this](std::pair<const size_t, NestedTest>& kv) {
                                   return this->tests.contains(kv.first);
                               });

        if (it == to_paste.end()) {
            // If the id is free don't do anything
            break;
        }

        auto& [id, nt] = *it;
        size_t new_id;
        do {
            new_id = ++this->id_counter;
        } while (to_paste.contains(new_id));

        // Update parents children_idx to use new id
        size_t parent_id = std::visit(ParentIDVisitor(), nt);
        if (to_paste.contains(parent_id)) {
            auto& parent = to_paste[parent_id];
            assert(std::holds_alternative<Group>(parent));
            Group& parent_group = std::get<Group>(parent);
            auto& children_ids = parent_group.children_ids;

            size_t count = std::erase(children_ids, id);
            assert(count == 1);

            children_ids.push_back(new_id);
        }

        // Update groups children parent_id
        if (std::holds_alternative<Group>(nt)) {
            auto& group_nt = std::get<Group>(nt);
            for (size_t child_id : group_nt.children_ids) {
                assert(to_paste.contains(child_id));
                std::visit(SetParentIDVisitor{new_id}, to_paste[child_id]);
            }
        }

        // Replace key value in map
        auto node = to_paste.extract(it);
        node.key() = new_id;
        std::visit(SetIDVisitor{new_id}, node.mapped());
        to_paste.insert(std::move(node));
    }

    // Insert into passed in group
    for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
        auto& [id, nt] = *it;

        size_t parent_id = std::visit(ParentIDVisitor(), nt);
        if (!to_paste.contains(parent_id)) {
            group->children_ids.push_back(id);
            std::visit(SetParentIDVisitor{group->id}, nt);
        }
    }

    group->flags |= GROUP_OPEN;
    this->tests.merge(to_paste);
}

void AppState::move(Group* group, size_t idx) noexcept {
    // Not moving into itself
    assert(!this->selected_tests.contains(group->id));

    for (size_t id : this->selected_tests) {
        assert(this->tests.contains(id));

        if (this->parent_selected(id)) {
            continue;
        }

        // Remove from old parent's children
        size_t old_parent = std::visit(ParentIDVisitor(), this->tests.at(id));
        assert(this->tests.contains(old_parent));
        assert(std::holds_alternative<Group>(this->tests.at(old_parent)));
        Group& old_parent_group = std::get<Group>(this->tests.at(old_parent));

        size_t count = std::erase(old_parent_group.children_ids, id);
        assert(count == 1);

        // Set to new parent
        std::visit(SetParentIDVisitor{group->id}, this->tests.at(id));

        if (idx >= group->children_ids.size()) {
            group->children_ids.push_back(id);
        } else {
            group->children_ids.insert(group->children_ids.begin() + static_cast<uint32_t>(idx),
                                       id);
            idx += 1;
        }
    }

    group->flags |= GROUP_OPEN;
}

void AppState::sort(Group& group) noexcept {
    std::sort(group.children_ids.begin(), group.children_ids.end(),
              [this](size_t a, size_t b) { return test_comp(this->tests, a, b); });
}

bool AppState::filter(Group& group) noexcept {
    bool result = true;
    for (size_t child_id : group.children_ids) {
        result &= this->filter(&this->tests[child_id]);
    }
    result &= !contains(group.name, this->tree_view_filter);

    if (result) {
        group.flags &= ~GROUP_OPEN;
    } else {
        group.flags |= GROUP_OPEN;
    }
    return result;
}

bool AppState::filter(Test& test) noexcept {
    return !contains(test.endpoint, this->tree_view_filter);
}

bool AppState::filter(NestedTest* nt) noexcept {
    bool filter = std::visit([this](auto& elem) { return this->filter(elem); }, *nt);

    if (filter) {
        this->filtered_tests.insert(std::visit(IDVisitor(), *nt));
    } else {
        this->filtered_tests.erase(std::visit(IDVisitor(), *nt));
    }
    return filter;
}

void AppState::save_file() noexcept {
    assert(this->filename.has_value());

    std::ofstream out(this->filename.value());
    if (!out) {
        Log(LogLevel::Error, "Failed to save to file '%s'", this->filename->c_str());
        return;
    }

    SaveState save{};
    save.save(*this);
    save.finish_save();
    Log(LogLevel::Info, "Saving to '%s': %zuB", this->filename->c_str(), save.original_size);
    if (!save.write(out)) {
        Log(LogLevel::Error, "Failed to save, likely size exeeds maximum");
    }
}

void AppState::open_file() noexcept {
    assert(this->filename.has_value());

    std::ifstream in(this->filename.value());
    if (!in) {
        Log(LogLevel::Error, "Failed to open file \"%s\"", this->filename->c_str());
        return;
    }

    SaveState save{};
    if (!save.read(in)) {
        Log(LogLevel::Error, "Failed to read, likely file is invalid or size exeeds maximum");
        return;
    }

    Log(LogLevel::Info, "Loading from '%s': %zuB", this->filename->c_str(), save.original_size);
    if (!save.can_load(*this) || save.load_idx != save.original_size) {
        Log(LogLevel::Error, "Failed to load, likely file is invalid");
        return;
    }

    save.reset_load();
    save.load(*this);
    this->post_open();
}

AppState::AppState(HelloImGui::RunnerParams* _runner_params) noexcept
    : runner_params(_runner_params) {
    this->undo_history.reset_undo_history(this);
}

bool status_match(const std::string& match, int status) noexcept {
    auto status_str = to_string(status);
    for (size_t i = 0; i < match.size() && i < 3; i++) {
        if (std::tolower(match[i]) == 'x') {
            continue;
        }
        if (match[i] != status_str[i]) {
            return false;
        }
    }
    return true;
}

const char* body_match(const VariablesMap& vars, const Test* test,
                       const httplib::Result& result) noexcept {
    if (test->response.body_type == RESPONSE_ANY) {
        return nullptr; // Skip checks
    }

    if (result->has_header("Content-Type")) {
        ContentType to_match = response_content_type(&test->response);

        ContentType content_type = parse_content_type(result->get_header_value("Content-Type"));

        if (to_match != content_type) {
            return "Unexpected Response Content-Type";
        }

        if (!std::visit(EmptyVisitor(), test->response.body)) {
            if (test->response.body_type == RESPONSE_JSON) {
                assert(std::holds_alternative<std::string>(test->response.body));
                const char* err = json_validate(
                    replace_variables(vars, std::get<std::string>(test->response.body)),
                    result->body);
                if (err) {
                    return err;
                }
            } else {
                assert(std::holds_alternative<std::string>(test->response.body));
                if (replace_variables(vars, std::get<std::string>(test->response.body)) !=
                    result->body) {
                    return "Unexpected Response Body";
                }
            }
        }
    }

    return nullptr;
}

// TODO: Add wildcards to header matching (easy)
const char* header_match(const VariablesMap& vars, const Test* test,
                         const httplib::Result& result) noexcept {
    httplib::Headers headers = response_headers(vars, test);
    for (const auto& elem : test->response.cookies.elements) {
        if (elem.flags & PARTIAL_DICT_ELEM_ENABLED) {
            headers.emplace("Set-Cookie", elem.key + "=" + elem.data.data);
        }
    }

    for (const auto& [key, value] : headers) {
        bool found = false;
        for (const auto& [match_key, match_value] : result->headers) {
            if (key == match_key && contains(match_value, value)) {
                found = true;
                break;
            }
        }

        if (!found) {
            return "Unexpected Response Headers";
        }
    }

    return nullptr;
}

bool test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result, const VariablesMap& vars) noexcept {
    bool success = true;
    switch (http_result.error()) {
    case httplib::Error::Success: {
        if (!status_match(test->response.status, http_result->status)) {
            success = false;

            test_result->status.store(STATUS_ERROR);
            test_result->verdict = "Unexpected Response Status";
            break;
        }

        char const* err = body_match(vars, test, http_result);
        if (err) {
            success = false;

            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        err = header_match(vars, test, http_result);
        if (err) {
            success = false;

            test_result->status.store(STATUS_ERROR);
            test_result->verdict = err;
            break;
        }

        test_result->status.store(STATUS_OK);
        test_result->verdict = "Success";
    } break;
    case httplib::Error::Canceled:
        success = false;

        test_result->status.store(STATUS_CANCELLED);
        break;
    default:
        success = false;

        test_result->status.store(STATUS_ERROR);
        test_result->verdict = to_string(http_result.error());
        break;
    }

    test_result->http_result = std::forward<httplib::Result>(http_result);

    return success;
}

httplib::Client make_client(const std::string& hostname, const ClientSettings& settings) noexcept {
    httplib::Client cli(hostname);

    cli.set_compress(settings.flags & CLIENT_COMPRESSION);
    cli.set_follow_location(settings.flags & CLIENT_FOLLOW_REDIRECTS);
    cli.set_keep_alive(settings.flags & CLIENT_KEEP_ALIVE);

    cli.set_connection_timeout(settings.seconds_timeout);

    switch (settings.auth.index()) {
    case AUTH_NONE:
        break;
    case AUTH_BASIC: {
        assert(std::holds_alternative<AuthBasic>(settings.auth));
        const AuthBasic* basic = &std::get<AuthBasic>(settings.auth);
        cli.set_basic_auth(basic->name, basic->password);
    } break;
    case AUTH_BEARER_TOKEN: {
        assert(std::holds_alternative<AuthBearerToken>(settings.auth));
        const AuthBearerToken* token = &std::get<AuthBearerToken>(settings.auth);
        cli.set_bearer_token_auth(token->token);
    } break;
    }

    if (settings.flags & CLIENT_PROXY) {
        cli.set_proxy(settings.proxy_host, settings.proxy_port);

        switch (settings.proxy_auth.index()) {
        case AUTH_NONE:
            break;
        case AUTH_BASIC: {
            assert(std::holds_alternative<AuthBasic>(settings.proxy_auth));
            const AuthBasic* basic = &std::get<AuthBasic>(settings.proxy_auth);
            cli.set_proxy_basic_auth(basic->name, basic->password);
        } break;
        case AUTH_BEARER_TOKEN: {
            assert(std::holds_alternative<AuthBearerToken>(settings.proxy_auth));
            const AuthBearerToken* token = &std::get<AuthBearerToken>(settings.proxy_auth);
            cli.set_proxy_bearer_token_auth(token->token);
        } break;
        }
    }

    return cli;
}

bool execute_test(AppState* app, const Test* test, const VariablesMap& vars, httplib::Client& cli,
                  const std::unordered_map<std::string, std::string>* overload_cookies) noexcept {

    const auto params = request_params(vars, test);
    const auto headers = request_headers(vars, test, overload_cookies);

    const auto req_body = request_body(vars, test);
    std::string content_type = req_body.content_type;
    std::string body = req_body.body;

    auto [host, dest] = split_endpoint(replace_variables(vars, test->endpoint));

    std::string params_dest = httplib::append_query_params(dest, params);

    TestResult* test_result = &app->test_results.at(test->id);

    test_result->running.store(true);
    test_result->status.store(STATUS_RUNNING);
    test_result->req_body = body;
    test_result->req_content_type = content_type;
    test_result->req_endpoint = host + params_dest;
    test_result->req_headers = headers;

    auto progress = [app, test, test_result](size_t current, size_t total) -> bool {
        // Missing
        if (!app->test_results.contains(test->id)) {
            return false;
        }

        // Stopped
        if (!test_result->running.load()) {
            test_result->status.store(STATUS_CANCELLED);

            return false;
        }

        test_result->progress_total = total;
        test_result->progress_current = current;
        test_result->verdict =
            to_string(static_cast<float>(current * 100) / static_cast<float>(total)) + "% ";

        return true;
    };

    httplib::Result result;

    switch (test->type) {
    case HTTP_GET:
        result = cli.Get(params_dest, headers, progress);
        break;
    case HTTP_POST:
        result = cli.Post(params_dest, headers, body, content_type, progress);
        break;
    case HTTP_PUT:
        result = cli.Put(params_dest, headers, body, content_type, progress);
        break;
    case HTTP_PATCH:
        result = cli.Patch(params_dest, headers, body, content_type, progress);
        break;
    case HTTP_DELETE:
        result = cli.Delete(params_dest, headers, body, content_type, progress);
        break;
    }

    // Time to brute force library because request_headers_ is a private field!
    for (const char* possible_header : RequestHeadersLabels) {
        if (result.has_request_header(possible_header)) {
            std::string header_value = result.get_request_header_value(possible_header);

            auto [search_begin, search_end] = test_result->req_headers.equal_range(possible_header);
            bool has_same_header_value = false;
            for (auto it = search_begin; it != search_end; it++) {
                if (it->second == header_value) {
                    has_same_header_value = true;
                    break;
                }
            }

            if (!has_same_header_value) {
                test_result->req_headers.emplace_hint(search_begin, possible_header, header_value);
            }
        }
    }

    if (result.error() == httplib::Error::Success) {
        test_result->res_body = format_response_body(result->body);
    }

    if (!app->test_results.contains(test->id)) {
        return false;
    }

    test_result->running.store(false);

    return test_analysis(app, test, test_result, std::move(result), vars);
}

bool is_parent_id(const AppState* app, size_t group_id, size_t needle) noexcept {
    assert(app->tests.contains(group_id));

    while (app->tests.contains(group_id)) {
        const NestedTest& nt = app->tests.at(group_id);

        assert(std::holds_alternative<Group>(nt));
        Group const* group = &std::get<Group>(nt);
        if (group->parent_id == needle) {
            return true;
        }

        group_id = group->parent_id;
    }

    return false;
}

// TODO: Replace existing similar functionality with calls to this function
// TODO: Add tests for this function
void iterate_over_nested_children(const AppState* app, size_t* id, size_t* child_idx,
                                  size_t breakpoint_group) noexcept {
    assert(app->tests.contains(*id));
    const NestedTest& nt = app->tests.at(*id);

    assert(std::holds_alternative<Group>(nt));
    Group const* group = &std::get<Group>(nt);

    if (*child_idx < group->children_ids.size()) {
        size_t child_id = group->children_ids.at(*child_idx);
        assert(app->tests.contains(child_id));
        const NestedTest& child_nt = app->tests.at(child_id);

        switch (child_nt.index()) {
        case GROUP_VARIANT: {
            assert(std::holds_alternative<Group>(child_nt));
            const Group& child_group = std::get<Group>(child_nt);

            if (!child_group.children_ids.empty()) {
                *id = child_group.id;
                *child_idx = 0;
                return;
            }
            // Fallthrough as we can skip entering this group
        }
        case TEST_VARIANT: {
            // Goto the next element if not the last element, otherwise go back to parent
            if (*child_idx < group->children_ids.size() - 1) {
                (*child_idx)++;
                return;
            }
        } break;
        }
    }

    // Visited all children, go back to parent
    //
    // Iterates while in tests
    // group is always the previously visited group
    *id = group->parent_id;
    while (app->tests.contains(*id) && *id != breakpoint_group) {
        const NestedTest& parent_nt = app->tests.at(*id);

        assert(std::holds_alternative<Group>(parent_nt));
        const Group& parent_group = std::get<Group>(parent_nt);

        // Find the next element after previous group
        for (size_t i = 0; i < parent_group.children_ids.size(); i++) {
            if (parent_group.children_ids.at(i) == group->id) {
                *child_idx = i + 1;
                break;
            }

            assert(i < parent_group.children_ids.size() && "Didn't find matching ID");
        }

        if (*child_idx < parent_group.children_ids.size()) {
            break;
        }

        group = &std::get<Group>(app->tests.at(*id));
        *id = group->parent_id;
    }
}

void run_dynamic_tests(AppState* app, const NestedTest& nt) noexcept {
    assert(std::holds_alternative<Group>(nt));
    const Group& group = std::get<Group>(nt);
    assert(group.cli_settings.has_value());
    assert(group.cli_settings->flags & CLIENT_DYNAMIC);

    std::vector<size_t> test_queue_ids = {};
    if (group.children_ids.size() < 0) {
        return;
    }

    // Make a test queue
    size_t id = group.id;
    size_t child_idx = 0;
    while (id != group.parent_id && !group.children_ids.empty()) {
        assert(app->tests.contains(id));
        NestedTest* iterated_nt = &app->tests.at(id);

        assert(std::holds_alternative<Group>(*iterated_nt));
        Group* iterated_group = &std::get<Group>(*iterated_nt);

        assert(child_idx < iterated_group->children_ids.size());
        assert(app->tests.contains(iterated_group->children_ids.at(child_idx)));
        NestedTest* child_nt = &app->tests.at(iterated_group->children_ids.at(child_idx));

        if (std::holds_alternative<Test>(*child_nt)) {
            Test* child_test = &std::get<Test>(*child_nt);

            if (!(child_test->flags & TEST_DISABLED) && !app->parent_disabled(child_test->id)) {
                test_queue_ids.push_back(iterated_group->children_ids.at(child_idx));
            }
        }

        if (std::holds_alternative<Test>(*child_nt) &&
            !app->parent_disabled(std::get<Test>(*child_nt).id)) {
        }

        iterate_over_nested_children(app, &id, &child_idx, group.parent_id);
    }

    if (test_queue_ids.empty()) {
        return;
    }

    std::vector<Test> test_queue = {};
    test_queue.reserve(test_queue_ids.size());

    std::vector<VariablesMap> test_vars = {};
    test_vars.reserve(test_queue_ids.size());

    // Fetch needed data from test queue
    for (size_t queued_test_id : test_queue_ids) {
        assert(app->tests.contains(queued_test_id));
        assert(std::holds_alternative<Test>(app->tests.at(queued_test_id)));

        test_queue.push_back(std::get<Test>(app->tests.at(queued_test_id)));

        // Add cli settings from parent to a copy
        test_queue.back().cli_settings = app->get_cli_settings(queued_test_id);

        test_vars.push_back(app->get_test_variables(queued_test_id));
    }

    // Find a common host to make a cli
    std::string hostname =
        split_endpoint(replace_variables(test_vars.at(0), test_queue.at(0).endpoint)).first;

    for (size_t idx = 1; idx < test_queue.size(); idx++) {
        if (hostname !=
            split_endpoint(replace_variables(test_vars.at(idx), test_queue.at(idx).endpoint))
                .first) {
            Log(LogLevel::Error, "Dynamic tests must all share same host");
            return;
        }
    }

    // Insert test results
    for (size_t idx = 0; idx < test_queue_ids.size(); idx++) {
        assert(!app->test_results.contains(test_queue_ids.at(idx)));
        app->test_results.try_emplace(test_queue_ids.at(idx), test_queue.at(idx), true);
    }

    // Copies test queue and vars
    // Mutates cookies
    app->thr_pool.detach_task([app, hostname, test_queue, test_vars,
                               cookies = std::unordered_map<std::string, std::string>{},
                               cli_settings = group.cli_settings.value()]() mutable {
        assert(test_queue.size() == test_vars.size());

        httplib::Client cli = make_client(hostname, cli_settings);

        bool keep_running = true;

        for (size_t idx = 0; idx < test_queue.size(); idx++) {
            size_t id = test_queue.at(idx).id;

            if (app->test_results.contains(id)) {
                TestResult* result = &app->test_results.at(id);

                for (const auto& cookie : test_queue.at(idx).request.cookies.elements) {
                    if (cookie.flags & PARTIAL_DICT_ELEM_ENABLED) {
                        cookies[cookie.key] = cookie.data.data;
                    }
                }

                if (!keep_running) {
                    result->running.store(false);
                    result->status.store(STATUS_CANCELLED);
                    result->verdict = "Previous test failed";
                    continue;
                }

                if (result->running.load()) {
                    // Can run test

                    keep_running &=
                        execute_test(app, &test_queue.at(idx), test_vars.at(idx), cli, &cookies);

                    if (result->http_result.has_value() &&
                        result->http_result->error() == httplib::Error::Success) {
                        for (const auto& [key, value] : result->http_result.value()->headers) {
                            if (key != "Set-Cookie") {
                                continue;
                            }

                            size_t key_val_split = value.find("=");
                            std::string cookie_name = value.substr(0, key_val_split);
                            std::string cookie_value = value.substr(key_val_split + 1);

                            cookies[cookie_name] = cookie_value;
                        };
                    }
                } else {
                    keep_running = false;
                }
            }
        }
    });
}

void run_test(AppState* app, size_t test_id) noexcept {
    assert(app->tests.contains(test_id));
    NestedTest& nt = app->tests.at(test_id);

    switch (nt.index()) {
    case TEST_VARIANT: {
        assert(std::holds_alternative<Test>(nt));
        Test& test = std::get<Test>(nt);

        assert(!app->test_results.contains(test_id));
        app->test_results.try_emplace(test_id, test, true);

        // Copies test
        app->thr_pool.detach_task([app, test = test, vars = app->get_test_variables(test.id),
                                   cli_settings = app->get_cli_settings(test.id)]() {
            // Add cli settings from parent to a copy
            std::string host = split_endpoint(replace_variables(vars, test.endpoint)).first;
            httplib::Client cli = make_client(host, cli_settings);
            return execute_test(app, &test, vars, cli);
        });
    } break;
    case GROUP_VARIANT: {
        run_dynamic_tests(app, nt);
    } break;
    }
}

void run_tests(AppState* app, const std::vector<size_t>& test_ids) noexcept {
    app->thr_pool.purge();
    app->test_results.clear();

    app->runner_params->dockingParams.dockableWindowOfName("Results")->focusWindowAtNextFrame =
        true;

    for (size_t id : test_ids) {
        assert(app->tests.contains(id));
        run_test(app, id);
    }
}

void rerun_test(AppState* app, TestResult* result) noexcept {
    if (!app->tests.contains(result->original_test.id)) {
        Log(LogLevel::Error, "Missing original test");
        return;
    }

    if (!std::holds_alternative<Test>(app->tests.at(result->original_test.id))) {
        Log(LogLevel::Error, "Missing original test");
        return;
    }

    result->status = STATUS_WAITING;
    result->verdict = "0%";

    Test test = std::get<Test>(app->tests.at(result->original_test.id));

    // Copies test
    app->thr_pool.detach_task([app, test = test, vars = app->get_test_variables(test.id),
                               cli_settings = app->get_cli_settings(test.id)]() {
        // Add cli settings from parent to a copy
        std::string host = split_endpoint(replace_variables(vars, test.endpoint)).first;
        httplib::Client cli = make_client(host, cli_settings);
        return execute_test(app, &test, vars, cli);
    });
}

void stop_test(TestResult* result) noexcept {
    assert(result->running.load());

    result->status.store(STATUS_CANCELLED);
    result->running.store(false);
}

void stop_test(AppState* app, size_t id) noexcept {
    assert(app->test_results.contains(id));

    auto& result = app->test_results.at(id);
    stop_test(&result);
}

void stop_tests(AppState* app) noexcept {
    for (auto& [id, result] : app->test_results) {
        if (result.running.load()) {
            stop_test(&result);
        }
    }

    app->thr_pool.purge();
}

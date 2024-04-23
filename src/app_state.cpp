#include "app_state.hpp"

bool AppState::is_running_tests() const noexcept {
    for (const auto& [id, result] : this->test_results) {
        if (result.running.load()) {
            return true;
        }
    }

    return false;
}

void AppState::stop_test(TestResult& result) noexcept {
    assert(result.running.load());

    result.status.store(STATUS_CANCELLED);
    result.running.store(false);
}

void AppState::stop_test(size_t id) noexcept {
    assert(this->test_results.contains(id));

    auto& result = this->test_results.at(id);
    this->stop_test(result);
}

void AppState::stop_tests() noexcept {
    for (auto& [id, result] : this->test_results) {
        if (result.running.load()) {
            this->stop_test(result);
        }
    }

    this->thr_pool.purge();
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

bool AppState::parent_disabled(const NestedTest* nt) noexcept {
    assert(nt);

    // OPTIM: maybe add some cache for every test that clears every frame?
    // if performance becomes a problem
    size_t id = std::visit(ParentIDVisitor(), *nt);
    while (id != -1ull) {
        assert(this->tests.contains(id));
        nt = &this->tests.at(id);

        assert(std::holds_alternative<Group>(*nt));
        const Group& group = std::get<Group>(*nt);
        if (group.flags & GROUP_DISABLED) {
            return true;
        }

        id = group.parent_id;
    }
    return false;
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

        std::visit(SetParentIDVisitor(parent_group.id), child);
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
    for (auto test_id : this->selected_tests) {
        if (this->parent_selected(test_id)) {
            continue;
        }

        for (auto it = parent_group.children_ids.begin(); it != parent_group.children_ids.end();
             it++) {
            if (*it == test_id) {
                parent_group.children_ids.erase(it);
                break;
            }
        }
    }

    parent_group.flags |= GROUP_OPEN;
    auto id = ++this->id_counter;
    auto new_group = Group{
        .parent_id = parent_group.id,
        .id = id,
        .flags = GROUP_OPEN,
        .name = "New group",
        .cli_settings = {},
        .children_ids = {},
    };

    // Copy selected to new group
    std::copy_if(this->selected_tests.begin(), this->selected_tests.end(),
                 std::back_inserter(new_group.children_ids), [this](size_t id) {
                     assert(this->tests.contains(id));
                     return !this->parent_selected(id);
                 });

    // Set selected parent id to new group's id
    std::for_each(this->selected_tests.begin(), this->selected_tests.end(),
                  [this, id](size_t test_id) {
                      assert(this->tests.contains(test_id));
                      std::visit(SetParentIDVisitor(id), this->tests.at(test_id));
                  });

    // Add new group to original parent's children
    parent_group.children_ids.push_back(id);

    this->tests.emplace(id, new_group);
}

void AppState::copy() noexcept {
    std::unordered_map<size_t, NestedTest> to_copy;

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
    std::unordered_map<size_t, NestedTest> to_paste;
    this->clipboard.load(to_paste);
    this->clipboard.reset_load();

    // Increments used ids
    // for tests updates id, parent children_idx (if parent present)
    // for groups should also update all children parent_id
    for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
        auto& [id, nt] = *it;

        if (!this->tests.contains(id)) {
            // If the id is free don't do anything
            continue;
        }

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
                std::visit(SetParentIDVisitor(new_id), to_paste[child_id]);
            }
        }

        // Replace key value in map
        auto node = to_paste.extract(it);
        node.key() = new_id;
        std::visit(SetIDVisitor(new_id), node.mapped());
        to_paste.insert(std::move(node));
    }

    // Insert into passed in group
    for (auto it = to_paste.begin(); it != to_paste.end(); it++) {
        auto& [id, nt] = *it;

        size_t parent_id = std::visit(ParentIDVisitor(), nt);
        if (!to_paste.contains(parent_id)) {
            group->children_ids.push_back(id);
            std::visit(SetParentIDVisitor(group->id), nt);
        }
    }

    group->flags |= GROUP_OPEN;
    this->tests.merge(to_paste);
    this->select_with_children(group->id);
}

void AppState::move(Group* group) noexcept {
    for (size_t id : this->selected_tests) {
        assert(this->tests.contains(id));

        if (this->parent_selected(id)) {
            continue;
        }

        // Remove from old parent's children
        size_t old_parent = std::visit(ParentIDVisitor(), this->tests[id]);
        assert(this->tests.contains(old_parent));
        assert(std::holds_alternative<Group>(this->tests.at(old_parent)));
        std::erase(std::get<Group>(this->tests.at(old_parent)).children_ids, id);

        // Set to new parent
        std::visit(SetParentIDVisitor(group->id), this->tests.at(id));

        group->children_ids.push_back(id);
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
    if (save.can_load(*this) && save.load_idx == save.original_size - 1) {
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

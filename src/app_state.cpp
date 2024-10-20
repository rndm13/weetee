#include "app_state.hpp"

#include "hello_imgui/hello_imgui.h"
#include "hello_imgui/hello_imgui_assets.h"
#include "hello_imgui/hello_imgui_logger.h"

#include "hello_imgui/internal/platform/ini_folder_locations.h"
#include "hello_imgui/runner_params.h"
#include "http.hpp"
#include "partial_dict.hpp"
#include "tests.hpp"
#include "utility"
#include "utils.hpp"

#include "algorithm"
#include "filesystem"
#include "fstream"
#include "iterator"
#include <cstdio>

void BackupConfig::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->time_to_backup);
    save->save(this->local_to_keep);
    save->save(this->remote_to_keep);
    save->save(this->local_dir);
}

bool BackupConfig::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->time_to_backup)) {
        return false;
    }
    if (!save->can_load(this->local_to_keep)) {
        return false;
    }
    if (!save->can_load(this->remote_to_keep)) {
        return false;
    }
    if (!save->can_load(this->local_dir)) {
        return false;
    }

    return true;
}

void BackupConfig::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->time_to_backup);
    save->load(this->local_to_keep);
    save->load(this->remote_to_keep);
    save->load(this->local_dir);
}

std::string BackupConfig::get_default_local_dir() const noexcept {
    return HelloImGui::IniFolderLocation(HelloImGui::IniFolderType::AppExecutableFolder) + FS_SLASH
           "backups" FS_SLASH;
}

std::string BackupConfig::get_local_dir() const noexcept {
    return this->local_dir.value_or(this->get_default_local_dir());
}

void UserConfig::save(SaveState* save) const noexcept {
    assert(save);

    save->save(this->sync_hostname);
    save->save(this->sync_session.status);
    save->save(this->sync_session.data);
    save->save(this->sync_name);
    save->save(this->sync_password); // Save a password clientside for future encryption
                                     // Worry about it being in plain text?
    save->save(this->language);
    save->save(this->backup);
}

bool UserConfig::can_load(SaveState* save) const noexcept {
    assert(save);

    if (!save->can_load(this->sync_hostname)) {
        return false;
    }
    if (!save->can_load(this->sync_session.status)) {
        return false;
    }
    if (!save->can_load(this->sync_session.data)) {
        return false;
    }
    if (!save->can_load(this->sync_name)) {
        return false;
    }
    if (!save->can_load(this->sync_password)) {
        return false;
    }
    if (!save->can_load(this->language)) {
        return false;
    }
    if (save->save_version >= 2 && !save->can_load(this->backup)) {
        return false;
    }

    return true;
}

void UserConfig::load(SaveState* save) noexcept {
    assert(save);

    save->load(this->sync_hostname);
    save->load(this->sync_session.status);
    save->load(this->sync_session.data);
    save->load(this->sync_name);
    save->load(this->sync_password);
    save->load(this->language);
    if (save->save_version >= 2) {
        save->load(this->backup);
    }
}

void UserConfig::open_file() noexcept {
    std::string filename =
        HelloImGui::IniFolderLocation(HelloImGui::IniFolderType::AppUserConfigFolder) +
        UserConfig::filename;

    std::ifstream in(filename);
    if (!in) {
        Log(LogLevel::Error, "Failed to load user config in %s", filename.c_str());
        return;
    }

    SaveState save{};
    if (!save.read(in) || !save.can_load(*this) || save.load_idx != save.original_size) {
        Log(LogLevel::Error,
            "Failed to read user config in '%s', likely file is invalid or size exceeds maximum",
            filename.c_str());
        Log(LogLevel::Warning, "Copying the old user config file, and creating a new one");

        std::string new_filename = filename + ".old";

        if (std::filesystem::exists(new_filename)) {
            std::filesystem::remove(new_filename);
        }
        if (!std::filesystem::copy_file(filename, new_filename)) {
            Log(LogLevel::Error, "Failed to copy old user config file '%s'", new_filename.c_str());
        }

        // Create a new config with default settings
        *this = {};
        this->save_file();
    }

    save.reset_load();
    save.load(*this);
}

void UserConfig::save_file() noexcept {
    std::string filename =
        HelloImGui::IniFolderLocation(HelloImGui::IniFolderType::AppUserConfigFolder) +
        UserConfig::filename;

    std::ofstream out(filename);
    if (!out) {
        Log(LogLevel::Error, "Failed to save user config in '%s'", filename.c_str());
        return;
    }

    SaveState save{};
    save.save(*this);
    save.finish_save();

    if (!save.write(out)) {
        Log(LogLevel::Error, "Failed to save", filename.c_str());
        return;
    }
}

std::string get_saved_path(const SavedFile& saved) {
    switch (saved.index()) {
    case SAVED_FILE_NONE:
        return "Untitled";
    case SAVED_FILE_REMOTE:
        return std::get<RemoteFile>(saved).filename;
    case SAVED_FILE_LOCAL:
        return std::get<LocalFile>(saved).filename;
    }
    return "Unknown";
}

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
    for (const auto& [id, results] : this->test_results) {
        for (const auto& result : results) {
            if (result.running.load()) {
                return true;
            }
        }
    }

    return false;
}

void AppState::save(SaveState* save) const noexcept {
    assert(save);

    // This save is obviously going to be pretty big so reserve instantly for speed
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

    this->runner_params->dockingParams.dockableWindowOfName("Editor###win_editor")
        ->focusWindowAtNextFrame = true;
    if (this->editor.open_tabs.contains(id)) {
        this->editor.open_tabs[id].just_opened = true;
    } else {
        this->editor.open_tabs[id] = EditorTab{
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
    this->editor.open_tabs.clear();
    this->tree_view.selected_tests.clear();
    this->undo_history.reset_undo_history(this);
}

void AppState::post_undo() noexcept {
    for (auto it = this->editor.open_tabs.begin(); it != this->editor.open_tabs.end();) {
        if (!this->tests.contains(it->first)) {
            it = this->editor.open_tabs.erase(it);
        } else {
            ++it;
        }
    }

    // remove missing tests from selected
    for (auto it = this->tree_view.selected_tests.begin();
         it != this->tree_view.selected_tests.end();) {
        if (!this->tests.contains(*it)) {
            it = this->tree_view.selected_tests.erase(it);
        } else {
            ++it;
        }
    }

    // add newly selected if parent is selected
    for (auto& [id, nt] : this->tests) {
        if (this->parent_selected(id) && !this->tree_view.selected_tests.contains(id)) {
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

bool AppState::parent_disabled(size_t id) const noexcept {
    // OPTIM: maybe add some cache for every test that clears every frame?
    // if performance becomes a problem
    assert(this->tests.contains(id));
    id = std::visit(ParentIDVisitor(), this->tests.at(id));

    while (id != -1ull) {
        if (!this->tests.contains(id)) {
            break;
        }

        assert(this->tests.contains(id));
        const NestedTest* nt = &this->tests.at(id);

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

    return this->tree_view.selected_tests.contains(
        std::visit(ParentIDVisitor(), this->tests.at(id)));
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
    for (auto sel_id : this->tree_view.selected_tests) {
        if (!this->parent_selected(sel_id)) {
            result.push_back(sel_id);
        }
    }

    return result;
}

AppState::SelectAnalysisResult AppState::select_analysis() const noexcept {
    SelectAnalysisResult result;
    result.top_selected_count = this->tree_view.selected_tests.size();

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

    for (auto test_id : this->tree_view.selected_tests) {
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

    for (size_t child_id : to_delete) {
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

    this->editor.open_tabs.erase(id);
    this->tree_view.selected_tests.erase(id);
}

void AppState::delete_selected() noexcept {
    for (auto test_id : this->select_top_layer()) {
        this->delete_test(test_id);
    }
}

void AppState::enable_selected(bool enable) noexcept {
    for (size_t it_idx : this->tree_view.selected_tests) {
        assert(this->tests.contains(it_idx));
        NestedTest* it_nt = &this->tests.at(it_idx);

        switch (it_nt->index()) {
        case TEST_VARIANT: {
            assert(std::holds_alternative<Test>(*it_nt));
            Test* it_test = &std::get<Test>(*it_nt);
            if (enable) {
                it_test->flags &= ~TEST_DISABLED;
            } else {
                it_test->flags |= TEST_DISABLED;
            }
        } break;
        case GROUP_VARIANT: {
            assert(std::holds_alternative<Group>(*it_nt));
            Group* it_group = &std::get<Group>(*it_nt);
            if (enable) {
                it_group->flags &= ~GROUP_DISABLED;
            } else {
                it_group->flags |= GROUP_DISABLED;
            }
        } break;
        }
    }
}

void AppState::group_selected(size_t common_parent_id) noexcept {
    assert(this->tests.contains(common_parent_id));
    auto* parent_test = &this->tests[common_parent_id];
    assert(std::holds_alternative<Group>(*parent_test));
    auto& parent_group = std::get<Group>(*parent_test);

    // remove selected from old parent
    size_t count = std::erase_if(parent_group.children_ids,
                                 [&sel_tests = this->tree_view.selected_tests](size_t child_id) {
                                     return sel_tests.contains(child_id);
                                 });
    assert(count >= 1);

    auto id = ++this->id_counter;
    auto new_group = Group{
        .parent_id = parent_group.id,
        .id = id,
        .flags = GROUP_OPEN,
        .name = this->i18n.tv_new_group_name,
        .cli_settings = {},
        .children_ids = {},
        .variables = {},
    };

    // Copy selected to new group
    std::copy_if(this->tree_view.selected_tests.begin(), this->tree_view.selected_tests.end(),
                 std::back_inserter(new_group.children_ids), [this](size_t selected_id) {
                     assert(this->tests.contains(selected_id));
                     return !this->parent_selected(selected_id);
                 });

    // Set selected parent id to new group's id
    std::for_each(this->tree_view.selected_tests.begin(), this->tree_view.selected_tests.end(),
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

    to_copy.reserve(this->tree_view.selected_tests.size());
    for (auto sel_id : this->tree_view.selected_tests) {
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
    assert(!this->tree_view.selected_tests.contains(group->id));

    for (size_t id : this->tree_view.selected_tests) {
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
    result &= !str_contains(group.name, this->tree_view.filter);

    if (result) {
        group.flags &= ~GROUP_OPEN;
    } else {
        group.flags |= GROUP_OPEN;
    }
    return result;
}

bool AppState::filter(Test& test) noexcept {
    return !str_contains(test.endpoint, this->tree_view.filter);
}

bool AppState::filter(NestedTest* nt) noexcept {
    bool filter = std::visit([this](auto& elem) { return this->filter(elem); }, *nt);

    if (filter) {
        this->tree_view.filtered_tests.insert(std::visit(IDVisitor(), *nt));
    } else {
        this->tree_view.filtered_tests.erase(std::visit(IDVisitor(), *nt));
    }
    return filter;
}

bool AppState::save_file(std::ostream& out) noexcept {
    if (!out) {
        Log(LogLevel::Error, "Failed to save to file");
        return false;
    }

    SaveState save{};
    save.save(*this);
    save.finish_save();
    if (!save.write(out)) {
        Log(LogLevel::Error, "Failed to save");
        return false;
    }

    return true;
}

bool AppState::open_file(std::istream& in) noexcept {
    if (!in) {
        Log(LogLevel::Error, "Failed to open file");
        return false;
    }

    SaveState save{};
    if (!save.read(in)) {
        Log(LogLevel::Error, "Failed to read, likely file is invalid or size exceeds maximum");
        return false;
    }

    if (!save.can_load(*this) || save.load_idx != save.original_size) {
        Log(LogLevel::Error, "Failed to load, likely file is invalid");
        return false;
    }

    save.reset_load();
    save.load(*this);
    this->post_open();
    return true;
}

void AppState::load_i18n() noexcept {
    if (!this->unit_testing) {
        std::ifstream in(HelloImGui::AssetFileFullPath(this->conf.language + ".json"));
        this->i18n = nlohmann::json::parse(in, nullptr, false, true).template get<I18N>();
    }
}

AppState::AppState(HelloImGui::RunnerParams* _runner_params, bool _unit_testing) noexcept
    : runner_params(_runner_params), unit_testing(_unit_testing) {
    this->undo_history.reset_undo_history(this);

    std::string conf_path =
        HelloImGui::IniFolderLocation(HelloImGui::IniFolderType::AppUserConfigFolder) + FS_SLASH
        "weetee" FS_SLASH;

    if (!std::filesystem::is_directory(conf_path)) {
        std::filesystem::remove(conf_path);
        std::filesystem::create_directory(conf_path);
    }

    std::string backup_path = this->conf.backup.get_default_local_dir();

    if (!std::filesystem::is_directory(backup_path)) {
        std::filesystem::remove(backup_path);
        std::filesystem::create_directory(backup_path);
    }

    this->conf.open_file();

    this->load_i18n();
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

        if (!test->response.body.empty()) {
            if (test->response.body_type == RESPONSE_JSON) {
                const char* err =
                    json_compare(replace_variables(vars, test->response.body), result->body);
                if (err) {
                    return err;
                }
            } else {
                if (replace_variables(vars, test->response.body) != result->body) {
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
            if (key == match_key && str_contains(match_value, value)) {
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

bool execute_test(AppState* app, const Test* test, size_t test_result_idx, httplib::Client& cli,
                  const std::unordered_map<std::string, std::string>* overload_cookies) noexcept {
    TestResult* test_result = &app->test_results.at(test->id).at(test_result_idx);

    const auto params = request_params(test_result->variables, test);
    const auto headers = request_headers(test_result->variables, test, overload_cookies);

    const auto req_body = request_body(test_result->variables, test);
    std::string content_type = req_body.content_type;
    std::string body = req_body.body;

    auto [host, dest] = split_endpoint(replace_variables(test_result->variables, test->endpoint));

    std::string params_dest = httplib::append_query_params(dest, params);

    assert(app->test_results.contains(test->id));
    assert(app->test_results.at(test->id).size() > test_result_idx);

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

        if (app->test_results.at(test->id).size() <= test_result->test_result_idx) {
            return false;
        }

        // Stopped
        if (!test_result->running.load()) {
            test_result->status.store(STATUS_CANCELLED);

            return false;
        }

        test_result->progress_current = current;
        test_result->progress_total = total;

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
        test_result->res_body = result->body;
        const char* err = json_format(test_result->res_body);
    }

    if (!app->test_results.contains(test->id)) {
        return false;
    }

    if (app->test_results.at(test->id).size() <= test_result_idx) {
        return false;
    }

    test_result->running.store(false);

    return test_analysis(app, test, test_result, std::move(result), test_result->variables);
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

        iterate_over_nested_children(app, &id, &child_idx, group.parent_id);
    }

    if (test_queue_ids.empty()) {
        return;
    }

    std::vector<Test> test_queue = {};
    test_queue.reserve(test_queue_ids.size());

    // Fetch needed data from test queue
    for (size_t queued_test_id : test_queue_ids) {
        assert(app->tests.contains(queued_test_id));
        assert(std::holds_alternative<Test>(app->tests.at(queued_test_id)));

        test_queue.push_back(std::get<Test>(app->tests.at(queued_test_id)));

        // Add cli settings from parent to a copy
        test_queue.back().cli_settings = app->get_cli_settings(queued_test_id);
    }

    std::unordered_map<size_t, std::vector<TestResult>> new_test_results;

    // Find a common host to make a cli
    std::string hostname = "";

    // Insert test results
    const ClientSettings& cli_settings = group.cli_settings.value();
    for (size_t idx = 0; idx < test_queue_ids.size(); idx++) {
        Test* test = &test_queue.at(idx);

        std::vector<TestResult> results = {};
        for (size_t rerun = 0; rerun < cli_settings.test_reruns; rerun++) {
            VariablesMap vars = app->get_test_variables(test->id);

            std::string new_hostname =
                split_endpoint(replace_variables(vars, test->endpoint)).first;

            if (hostname == "") {
                hostname = new_hostname;
            } else if (hostname != new_hostname) {
                Log(LogLevel::Error, "Dynamic tests must all share same host");
                return;
            }

            results.emplace_back(*test, rerun, true, vars);
        }

        assert(!app->test_results.contains(test_queue_ids.at(idx)) &&
               !new_test_results.contains(test_queue_ids.at(idx)));
        new_test_results.try_emplace(test_queue_ids.at(idx), std::move(results));
    }

    app->test_results.merge(new_test_results);

    // Copies test queue and vars
    // Mutates cookies

    for (size_t rerun = 0; rerun < cli_settings.test_reruns; rerun++) {
        app->thr_pool.detach_task([app, hostname, test_queue, cli_settings = cli_settings,
                                   rerun]() {
            bool keep_running = true;
            httplib::Client cli = make_client(hostname, cli_settings);
            std::unordered_map<std::string, std::string> cookies = {};

            for (size_t idx = 0; idx < test_queue.size(); idx++) {
                size_t id = test_queue.at(idx).id;

                if (app->test_results.contains(id)) {
                    TestResult* result = &app->test_results.at(id).at(rerun);

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
                            execute_test(app, &test_queue.at(idx), rerun, cli, &cookies);

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
}

void run_test(AppState* app, size_t test_id) noexcept {
    assert(app->tests.contains(test_id));
    NestedTest& nt = app->tests.at(test_id);

    switch (nt.index()) {
    case TEST_VARIANT: {
        assert(std::holds_alternative<Test>(nt));
        Test& test = std::get<Test>(nt);

        const ClientSettings& cli_settings = app->get_cli_settings(test.id);

        std::vector<TestResult> results = {};
        for (size_t rerun = 0; rerun < cli_settings.test_reruns; rerun++) {
            VariablesMap vars = app->get_test_variables(test.id);

            results.emplace_back(test, rerun, true, vars);
        }

        assert(!app->test_results.contains(test_id));
        app->test_results.try_emplace(test_id, std::move(results));

        for (size_t rerun = 0; rerun < cli_settings.test_reruns; rerun++) {
            // Copies test and ClientSettings
            app->thr_pool.detach_task([app, test = test, vars = app->get_test_variables(test.id),
                                       cli_settings = cli_settings, rerun]() {
                std::string host = split_endpoint(replace_variables(vars, test.endpoint)).first;
                httplib::Client cli = make_client(host, cli_settings);
                execute_test(app, &test, rerun, cli);
            });
        }
    } break;
    case GROUP_VARIANT: {
        run_dynamic_tests(app, nt);
    } break;
    }
}

void run_tests(AppState* app, const std::vector<size_t>& test_ids) noexcept {
    app->thr_pool.purge();
    app->test_results.clear();

    app->runner_params->dockingParams.dockableWindowOfName("Results###win_results")
        ->focusWindowAtNextFrame = true;

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
    result->verdict = "";
    result->progress_total = 0;
    result->progress_current = 0;
    result->open = false;
    result->original_test = std::get<Test>(app->tests.at(result->original_test.id));

    // Copies test
    app->thr_pool.detach_task(
        [app, result, cli_settings = app->get_cli_settings(result->original_test.id)]() {
            // Add cli settings from parent to a copy
            std::string host =
                split_endpoint(replace_variables(result->variables, result->original_test.endpoint))
                    .first;
            httplib::Client cli = make_client(host, cli_settings);
            execute_test(app, &result->original_test, result->test_result_idx, cli);
        });
}

bool is_test_running(AppState* app, size_t id) noexcept {
    if (!app->test_results.contains(id)) {
        return false;
    }

    for (auto& result : app->test_results.at(id)) {
        if (result.running.load()) {
            return true;
        }
    }

    return false;
}

void stop_test(TestResult* result) noexcept {
    assert(result->running.load());

    result->status.store(STATUS_CANCELLED);
    result->running.store(false);
}

void stop_test(AppState* app, size_t id) noexcept {
    assert(app->test_results.contains(id));

    for (auto& result : app->test_results.at(id)) {
        stop_test(&result);
    }
}

void stop_tests(AppState* app) noexcept {
    for (auto& [id, results] : app->test_results) {
        for (auto& result : results) {
            if (result.running.load()) {
                stop_test(&result);
            }
        }
    }

    app->thr_pool.purge();
}

void remote_file_list(AppState* app, bool sync,
                      Requestable<std::vector<std::string>>* result) noexcept {
    if (result == nullptr) {
        result = &app->sync.files;
    }

    httplib::Params params = {
        {"session_token", app->conf.sync_session.data},
    };

    auto proc = [](Requestable<std::vector<std::string>>& requestable, const std::string& data) {
        requestable.data.clear();

        auto json_data = nlohmann::json::parse(data, nullptr, false);

        if (json_data.is_discarded()) {
            requestable.error = "Failed to parse received JSON";
            requestable.status = REQUESTABLE_ERROR;
            return;
        }

        if (json_data.is_array()) {
            for (auto& item : json_data) {
                if (item.is_object() && item.contains("name")) {
                    requestable.data.push_back(item["name"]);
                }
            }
        }
    };

    if (sync) {
        execute_requestable_sync(app, *result, HTTP_GET, app->conf.sync_hostname, "/file-list", "",
                                 params, proc);
    } else {
        execute_requestable_async(app, *result, HTTP_GET, app->conf.sync_hostname, "/file-list", "",
                                  params, proc);
    }
}

void remote_file_open(AppState* app, const std::string& name) noexcept {
    httplib::Params params = {
        {"session_token", app->conf.sync_session.data},
        {"file_name", name},
    };
    auto proc = [app, name](Requestable<std::string>& requestable, const std::string& data) {
        app->saved_file = RemoteFile{name};

        requestable.data = data;
    };
    execute_requestable_async(app, app->sync.file_open, HTTP_GET, app->conf.sync_hostname, "/file",
                              "", params, proc);
};

void remote_file_delete(AppState* app, const std::string& name, bool sync) noexcept {
    httplib::Params params = {
        {"session_token", app->conf.sync_session.data},
        {"file_name", name},
    };

    auto proc = [app, &name](Requestable<bool>& requestable, const std::string& data) {
        app->sync.files = {};
        // Commented out because of race conditions when files could be invalidated
        // app->sync.files.data.erase(
        //     std::remove(app->sync.files.data.begin(), app->sync.files.data.end(), name));

        requestable.data = true;
    };

    if (sync) {
        execute_requestable_sync(app, app->sync.file_delete, HTTP_DELETE, app->conf.sync_hostname,
                                 "/file", "", params, proc);
    } else {
        execute_requestable_async(app, app->sync.file_delete, HTTP_DELETE, app->conf.sync_hostname,
                                  "/file", "", params, proc);
    }
};

void remote_file_rename(AppState* app, const std::string& old_name,
                        const std::string& new_name) noexcept {
    httplib::Params params = {
        {"session_token", app->conf.sync_session.data},
        {"file_name", old_name},
        {"new_file_name", new_name},
    };

    auto proc = [app, old_name, new_name](auto& requestable, const std::string& data) {
        app->sync.files = {};

        if (std::holds_alternative<RemoteFile>(app->saved_file) &&
            std::get<RemoteFile>(app->saved_file).filename == old_name) {
            std::get<RemoteFile>(app->saved_file).filename = new_name;
        }

        if (app->sync.filename == old_name) {
            app->sync.filename = new_name;
        }

        requestable.data = true;
    };
    execute_requestable_async(app, app->sync.file_delete, HTTP_PATCH, app->conf.sync_hostname,
                              "/file", "", params, proc);
};

void remote_file_save(AppState* app, const std::string& name, bool sync,
                      Requestable<bool>* result) noexcept {
    if (result == nullptr) {
        result = &app->sync.file_save;
    }

    std::stringstream out;
    app->save_file(out);
    std::string body = out.str();

    httplib::Params params = {
        {"session_token", app->conf.sync_session.data},
        {"file_name", name},
    };

    auto proc = [app, name](auto& requestable, const std::string& data) {
        requestable.data = true;

        if (std::find(app->sync.files.data.begin(), app->sync.files.data.end(), name) ==
            app->sync.files.data.end()) {
            app->sync.files.data.push_back(name);
        }
    };

    if (sync) {
        execute_requestable_sync(app, *result, HTTP_POST, app->conf.sync_hostname, "/file", body,
                                 params, proc);
    } else {
        execute_requestable_async(app, *result, HTTP_POST, app->conf.sync_hostname, "/file", body,
                                  params, proc);
    }
};

std::optional<BackupInfo> get_backup_info(const std::string& filename) noexcept {
    BackupInfo result{};

    size_t id_end = filename.rfind('.');
    size_t id_start = filename.rfind('_', id_end);
    size_t name_start = filename.rfind(FS_SLASH, id_start);
    if (name_start == std::string::npos) {
        name_start = 0;
    } else {
        name_start += 1; // Skip over FS_SLASH
    }

    if (id_end == std::string::npos || id_start == std::string::npos) {
        return std::nullopt;
    }

    result.id = atoi(filename.substr(id_start + 1, id_end - id_start - 1).c_str());
    result.name = filename.substr(name_start, id_start - name_start);

    return result;
}

// TODO: backup only when file was changed
void make_local_backup(AppState* app) noexcept {
    namespace fs = std::filesystem;

    std::string dir = app->conf.backup.get_local_dir();

    std::string name = get_filename(get_saved_path(app->saved_file));
    int64_t max_id = 0;

    // Find a max ID of a backup
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::optional<BackupInfo> opt_info = get_backup_info(entry.path());
        if (!opt_info.has_value()) {
            continue;
        }

        BackupInfo info = opt_info.value();

        if (info.name == name && info.id > max_id) {
            max_id = info.id;
        }
    }

    max_id += 1;

    std::string new_backup_path =
        app->conf.backup.get_local_dir() + name + '_' + to_string(max_id) + ".wt";

    std::ofstream out(new_backup_path);
    Log(LogLevel::Info, "Saving backup to local file '%s'", new_backup_path.c_str());
    if (app->save_file(out)) {
        Log(LogLevel::Info, "Successfully saved to '%s'!", new_backup_path.c_str());
    }

    // Remove entries with lower id
    for (const auto& entry : fs::directory_iterator(dir)) {
        std::optional<BackupInfo> opt_info = get_backup_info(entry.path());
        if (!opt_info.has_value()) {
            continue;
        }

        BackupInfo info = opt_info.value();
        if (info.name == name && info.id <= max_id - app->conf.backup.local_to_keep) {
            fs::remove(entry);
        }
    }
}

void make_remote_backup(AppState* app) noexcept {
    Log(LogLevel::Info, "Making a remote backup...");

    app->thr_pool.detach_task([app]() {
        Requestable<std::vector<std::string>> file_list;
        remote_file_list(app, true, &file_list);

        if (file_list.status != REQUESTABLE_FOUND) {
            // TODO: Show these errors and a success message when finishing
            // Log(LogLevel::Error, "Failed to fetch a list of remote files: %s",
            //     file_list.error.c_str());
            // Log(LogLevel::Error, "Failed to make a remote backup");
            return;
        }

        std::string name = get_filename(get_saved_path(app->saved_file));
        int64_t max_id = 0;

        // Find a max ID of a backup
        for (const std::string& filename : file_list.data) {
            std::optional<BackupInfo> opt_info = get_backup_info(filename);

            if (!opt_info.has_value()) {
                continue;
            }

            BackupInfo info = opt_info.value();
            if (info.name == name && info.id > max_id) {
                max_id = info.id;
            }
        }

        max_id += 1;

        std::string new_backup_filename = name + '_' + to_string(max_id) + ".wt";

        remote_file_save(app, new_backup_filename);

        // Remove entries with lower id
        for (const std::string& filename : file_list.data) {
            std::optional<BackupInfo> opt_info = get_backup_info(filename);
            if (!opt_info.has_value()) {
                continue;
            }

            BackupInfo info = opt_info.value();
            if (info.name == name && info.id <= max_id - app->conf.backup.remote_to_keep) {
                remote_file_delete(app, filename);
            }
        }
    });
}

void make_backups(AppState* app) noexcept {
    if (app->conf.backup.local_to_keep > 0) {
        make_local_backup(app);
    }
    if (app->conf.backup.remote_to_keep > 0) {
        make_remote_backup(app);
    }
}

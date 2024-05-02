#pragma once

#include "hello_imgui/runner_params.h"

#include "imgui.h"

#include "BS_thread_pool.hpp"

#include "partial_dict.hpp"
#include "save_state.hpp"
#include "test.hpp"

#include "cmath"
#include "optional"
#include "string"
#include "unordered_map"
#include "variant"

struct EditorTab {
    bool just_opened = true;
    size_t original_idx;
    std::string name;
};

struct AppState {
    // Save
    size_t id_counter = 0;

    static const Group root_initial;

    std::unordered_map<size_t, NestedTest> tests = {
        {0, root_initial},
    };
    std::string tree_view_filter;
    std::unordered_set<size_t> filtered_tests = {};

    std::unordered_set<size_t> selected_tests = {};
    std::unordered_map<size_t, EditorTab> opened_editor_tabs = {};

    // Keys are test ids
    std::unordered_map<size_t, TestResult> test_results;

    SaveState clipboard;
    UndoHistory undo_history;

    // Don't save

    std::optional<pfd::open_file> open_file_dialog;
    std::optional<pfd::open_file> import_swagger_file_dialog;
    std::optional<pfd::save_file> save_file_dialog;
    std::optional<pfd::save_file> export_swagger_file_dialog;
    std::optional<std::string> filename;

    BS::thread_pool thr_pool;

    ImFont* regular_font;
    ImFont* mono_font;
    ImFont* awesome_font;
    HelloImGui::RunnerParams* runner_params;

    bool tree_view_focused; // Updated every frame

    constexpr Group* root_group() noexcept {
        assert(this->tests.contains(0));
        assert(std::holds_alternative<Group>(this->tests[0]));

        return &std::get<Group>(this->tests[0]);
    }
    constexpr const Group* root_group() const noexcept {
        assert(this->tests.contains(0));
        assert(std::holds_alternative<Group>(this->tests.at(0)));

        return &std::get<Group>(this->tests.at(0));
    }
    bool is_running_tests() const noexcept;
    void stop_test(TestResult& result) noexcept;
    void stop_test(size_t id) noexcept;
    void stop_tests() noexcept;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    void editor_open_tab(size_t id) noexcept;
    // On undo/redo focus on new/changed tests
    void focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests) noexcept;

    void undo() noexcept;
    void post_undo() noexcept;
    void redo() noexcept;

    VariablesMap variables(size_t id) const noexcept;
    bool parent_disabled(size_t id) noexcept;
    bool parent_selected(size_t id) const noexcept;
    ClientSettings get_cli_settings(size_t id) const noexcept;
    std::vector<size_t> select_top_layer() noexcept;

    struct SelectAnalysisResult {
        bool group = false;
        bool test = false;
        bool same_parent = true;
        bool selected_root = false;
        size_t parent_id = -1ull;
        size_t top_selected_count;
    };

    SelectAnalysisResult select_analysis() const noexcept;
    template <bool select = true> void select_with_children(size_t id) noexcept {
        assert(this->tests.contains(id));

        if constexpr (select) {
            this->selected_tests.insert(id);
        } else if (!this->parent_selected(id)) {
            this->selected_tests.erase(id);
        }

        NestedTest& nt = this->tests.at(id);

        if (!std::holds_alternative<Group>(nt)) {
            return;
        }

        Group& group = std::get<Group>(nt);

        for (size_t child_id : group.children_ids) {
            assert(this->tests.contains(child_id));
            if constexpr (select) {
                this->selected_tests.insert(child_id);
            } else if (!this->parent_selected(id)) {
                this->selected_tests.erase(child_id);
            }

            select_with_children<select>(child_id);
        }
    }

    void move_children_up(Group* group) noexcept;

    void delete_children(const Group* group) noexcept;
    void delete_test(size_t id) noexcept;
    void delete_selected() noexcept;

    void group_selected(size_t common_parent_id) noexcept;

    void copy() noexcept;
    void cut() noexcept;

    constexpr bool can_paste() const noexcept { return this->clipboard.original_size > 0; }
    void paste(Group* group) noexcept;

    void move(Group* group) noexcept;
    void sort(Group& group) noexcept;

    // Returns true when the value should be filtered *OUT*
    bool filter(Group& group) noexcept;
    bool filter(Test& test) noexcept;
    bool filter(NestedTest* nt) noexcept;

    void save_file() noexcept;
    void open_file() noexcept;
    void post_open() noexcept;

    void import_swagger_paths(const nlohmann::json&) noexcept;
    void import_swagger_servers(const nlohmann::json&) noexcept;
    void import_swagger(const std::string& filename) noexcept;

    void export_swagger_paths(nlohmann::json&) const noexcept;
    void export_swagger_servers(nlohmann::json&) const noexcept;
    void export_swagger(const std::string& filename) const noexcept;

    AppState(HelloImGui::RunnerParams* _runner_params) noexcept;

    // no copy/move
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
};

httplib::Result make_request(AppState* app, const Test* test) noexcept;
void run_test(AppState* app, const Test* test, const VariablesMap& vars) noexcept;
void run_tests(AppState* app, const std::vector<Test>* tests) noexcept;

bool status_match(const std::string& match, int status) noexcept;
const char* body_match(const VariablesMap& vars, const Test* test,
                       const httplib::Result& result) noexcept;
const char* header_match(const VariablesMap&, const Test* test,
                         const httplib::Result& result) noexcept;
void test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result, const VariablesMap& vars) noexcept;

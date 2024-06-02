#pragma once

#include "hello_imgui/hello_imgui_assets.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/runner_params.h"

#include "i18n.hpp"
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

using HelloImGui::Log;
using HelloImGui::LogLevel;

struct EditorTab {
    bool just_opened = true;
    size_t original_idx;
    std::string name;
};

struct AppState {
    size_t id_counter = 0;

    static const Group root_initial;

    std::unordered_map<size_t, NestedTest> tests = {
        {0, root_initial},
    };
    size_t tree_view_last_selected_idx = 0;

    std::string tree_view_filter;
    std::unordered_set<size_t> filtered_tests = {};
    std::unordered_set<size_t> selected_tests = {};

    SaveState clipboard;
    UndoHistory undo_history;

    bool editor_show_homepage = true;
    std::unordered_map<size_t, EditorTab> editor_open_tabs = {};

    std::unordered_map<size_t, std::vector<TestResult>> test_results;
    size_t test_results_last_selected_id = 0;
    size_t test_results_last_selected_idx = 0;
    TestResultStatus test_results_filter = STATUS_OK;
    bool test_results_filter_cumulative = true;

    std::optional<std::string> filename;

    bool sync_show = false;
    // TODO: Save
    std::string sync_hostname = "https://weetee-sync.vercel.app";
    bool sync_wait = false;
    bool sync_logged_in = false;
    std::string sync_session = "";
    std::string sync_name;
    std::string sync_password;

    BS::thread_pool thr_pool;

    ImFont* regular_font;
    ImFont* mono_font;
    ImFont* awesome_font;
    HelloImGui::RunnerParams* runner_params;

    // TODO: Save
    std::string language = "en";
    I18N i18n;

    bool tree_view_focused; // Updated every frame

    Group* root_group() noexcept;
    const Group* root_group() const noexcept;

    bool is_running_tests() const noexcept;

    void save(SaveState* save) const noexcept;
    bool can_load(SaveState* save) const noexcept;
    void load(SaveState* save) noexcept;

    void editor_open_tab(size_t id) noexcept;

    // On undo/redo focus on new/changed tests
    void focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests) noexcept;

    void undo() noexcept;
    void redo() noexcept;
    void post_undo() noexcept;

    VariablesMap get_test_variables(size_t id) const noexcept;
    bool parent_disabled(size_t id) const noexcept;
    bool parent_selected(size_t id) const noexcept;
    ClientSettings get_cli_settings(size_t id) const noexcept;
    std::vector<size_t> select_top_layer() noexcept;

    struct SelectAnalysisResult {
        bool group = false;
        bool test = false;
        bool same_parent = true;
        bool selected_root = false;
        size_t parent_id = -1ull;
        size_t top_selected_count = 0;
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

    void enable_selected(bool enable = true) noexcept;

    void group_selected(size_t common_parent_id) noexcept;

    void copy() noexcept;
    void cut() noexcept;

    constexpr bool can_paste() const noexcept { return this->clipboard.original_size > 0; }
    void paste(Group* group) noexcept;

    void move(Group* group, size_t idx) noexcept;
    void sort(Group& group) noexcept;

    // Returns true when the value should be filtered *OUT*
    bool filter(Group& group) noexcept;
    bool filter(Test& test) noexcept;
    bool filter(NestedTest* nt) noexcept;

    void save_file() noexcept;
    void open_file() noexcept;
    void post_open() noexcept;

    void import_swagger_paths(const nlohmann::json& paths, const nlohmann::json& swagger) noexcept;
    void import_swagger_servers(const nlohmann::json&) noexcept;
    void import_swagger(const std::string& filename) noexcept;

    void export_swagger_paths(nlohmann::json&) const noexcept;
    void export_swagger_servers(nlohmann::json&) const noexcept;
    void export_swagger(const std::string& filename) const noexcept;

    void load_i18n() noexcept;

    AppState(HelloImGui::RunnerParams* _runner_params) noexcept;

    // no copy/move
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
};

httplib::Client make_client(const std::string& hostname, const ClientSettings& settings) noexcept;

template <class It> std::vector<size_t> get_tests_to_run(AppState* app, It begin, It end) noexcept {
    std::vector<size_t> tests_to_run;
    for (It it = begin; it != end; it++) {
        assert(app->tests.contains(*it));

        const NestedTest* nested_test = &app->tests.at(*it);
        switch (nested_test->index()) {

        case TEST_VARIANT: {
            if (app->get_cli_settings(*it).flags & CLIENT_DYNAMIC) {
                continue;
            }

            assert(std::holds_alternative<Test>(*nested_test));
            const auto& test = std::get<Test>(*nested_test);

            if (!(test.flags & TEST_DISABLED) && !app->parent_disabled(*it)) {
                tests_to_run.push_back(*it);
            }
        } break;
        case GROUP_VARIANT: {
            assert(std::holds_alternative<Group>(*nested_test));
            const auto& group = std::get<Group>(*nested_test);

            if (group.cli_settings.has_value() && (group.cli_settings->flags & CLIENT_DYNAMIC)) {
                if (!(group.flags & GROUP_DISABLED) && !app->parent_disabled(*it)) {
                    tests_to_run.push_back(*it);
                }
            }
        } break;
        }
    }
    return tests_to_run;
}

bool is_parent_id(const AppState* app, size_t group_id, size_t parent_id) noexcept;
void iterate_over_nested_children(const AppState* app, size_t* id, size_t* child_idx,
                                  size_t breakpoint_group) noexcept;

void run_dynamic_tests(AppState* app, const NestedTest& nt) noexcept;
void run_test(AppState* app, size_t test_id) noexcept;
bool execute_test(
    AppState* app, const Test* test, size_t test_result_idx, httplib::Client& cli,
    const std::unordered_map<std::string, std::string>* overload_cookies = nullptr) noexcept;
void run_tests(AppState* app, const std::vector<size_t>& tests) noexcept;
void rerun_test(AppState* app, TestResult* result) noexcept;

bool is_test_running(AppState* app, size_t id) noexcept;

void stop_test(TestResult* result) noexcept;
void stop_test(AppState* app, size_t id) noexcept;
void stop_tests(AppState* app) noexcept;

bool status_match(const std::string& match, int status) noexcept;
const char* body_match(const VariablesMap& vars, const Test* test,
                       const httplib::Result& result) noexcept;
const char* header_match(const VariablesMap&, const Test* test,
                         const httplib::Result& result) noexcept;
bool test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result, const VariablesMap& vars) noexcept;

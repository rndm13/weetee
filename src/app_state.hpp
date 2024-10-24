#pragma once

#include "hello_imgui/hello_imgui_assets.h"
#include "hello_imgui/hello_imgui_logger.h"
#include "hello_imgui/runner_params.h"

#include "i18n.hpp"
#include "imgui.h"

#include "BS_thread_pool.hpp"

#include "partial_dict.hpp"
#include "save_state_incl.hpp"
#include "tests.hpp"

#include "cmath"
#include "optional"
#include "string"
#include "unordered_map"
#include "variant"

// TODO: Add unit tests to app_state

using HelloImGui::Log;
using HelloImGui::LogLevel;

struct EditorTab {
    bool just_opened = true;
    size_t original_idx;
    std::string name;
};

enum RequestableStatus : uint8_t {
    REQUESTABLE_NONE,
    REQUESTABLE_WAIT,
    REQUESTABLE_ERROR,
    REQUESTABLE_FOUND,

    REQUESTABLE_COUNT,
};

template <class Data> struct Requestable {
    RequestableStatus status = REQUESTABLE_NONE;
    std::string error = "";
    Data data = {};
};

struct BackupConfig {
    uint32_t time_to_backup = 60 * 5;

    uint8_t local_to_keep = 5;
    uint8_t remote_to_keep = 0;

    std::optional<std::string> local_dir = std::nullopt;
    std::string get_default_local_dir() const;
    std::string get_local_dir() const;

    SAVE_STATE_SAVE_IMPL(
            time_to_backup, 0,
            local_to_keep, 0,
            remote_to_keep, 0,
            local_dir, 0);
};

struct UserConfig {
    std::string language = "en";

    std::string sync_hostname = "https://weetee-sync.vercel.app";
    Requestable<std::string> sync_session = {};
    std::string sync_name = "";
    std::string sync_password = "";

    BackupConfig backup = {};

    static constexpr const char* filename = FS_SLASH "weetee" FS_SLASH "user_config.wt";

    SAVE_STATE_SAVE_IMPL(
            sync_hostname, 0,
            sync_session.status, 0,
            sync_session.data, 0,
            sync_name, 0,
            sync_password, 0,
            language, 0,
            backup, 2);

    void open_file();
    void save_file();
};

struct SyncState {
    bool show = false;

    bool remember_me = false;

    Requestable<std::vector<std::string>> files = {};
    Requestable<std::string> file_open = {};
    Requestable<bool> file_delete = {};
    Requestable<bool> file_rename = {};
    Requestable<bool> file_save = {};

    std::string filename = "";
};

struct TreeViewState {
    size_t last_selected_idx = 0;

    std::string filter;
    std::unordered_set<size_t> filtered_tests = {};
    std::unordered_set<size_t> selected_tests = {};

    // Updated every frame, needed for shortcuts to work
    bool window_focused;
};

struct EditorState {
    bool show_homepage = true;
    std::unordered_map<size_t, EditorTab> open_tabs = {};
};

struct ResultsState {
    size_t last_selected_id = 0;
    size_t last_selected_idx = 0;
    TestResultStatus filter = STATUS_OK;
    bool filter_cumulative = true;
};

struct RemoteFile {
    std::string filename;
};

struct LocalFile {
    std::string filename;
};

enum SavedFileType : uint8_t {
    SAVED_FILE_NONE,
    SAVED_FILE_REMOTE,
    SAVED_FILE_LOCAL,
};

using SavedFile = std::variant<std::monostate, RemoteFile, LocalFile>;
std::string get_saved_path(const SavedFile& saved);

struct BackupState {
    float time_since_last_backup = 0;
};

struct SettingsState {
    bool show = false;

    std::string search = "";
};

struct AppState {
    size_t id_counter = 0;

    static const Group root_initial;

    using TestsType = std::unordered_map<size_t, NestedTest>;
    TestsType tests = {{0, root_initial}};
    std::unordered_map<size_t, std::vector<TestResult>> test_results = {};

    UserConfig conf = {};

    TreeViewState tree_view = {};
    EditorState editor = {};
    ResultsState results = {};
    SyncState sync = {};
    SettingsState settings = {};
    BackupState backup = {};

    SaveState clipboard;
    UndoHistory undo_history;

    SavedFile saved_file;

    BS::thread_pool thr_pool;

    ImFont* regular_font;
    ImFont* mono_font;
    ImFont* awesome_font;
    HelloImGui::RunnerParams* runner_params;

    I18N i18n;

    bool unit_testing = false;

    Group* root_group();
    const Group* root_group() const;

    bool is_running_tests() const;

    SAVE_STATE_RECOVER_SAVE_IMPL(id_counter, 0, tests, 0);
    SAVE_STATE_RECOVER_IMPL(size_t, id_counter, TestsType, tests);

    void editor_open_tab(size_t id);

    // On undo/redo focus on new/changed tests
    void focus_diff_tests(std::unordered_map<size_t, NestedTest>* old_tests);

    void undo();
    void redo();
    void post_undo();

    VariablesMap get_test_variables(size_t id) const;
    bool parent_disabled(size_t id) const;
    bool parent_selected(size_t id) const;
    ClientSettings get_cli_settings(size_t id) const;
    std::vector<size_t> select_top_layer();

    struct SelectAnalysisResult {
        bool group = false;
        bool test = false;
        bool same_parent = true;
        bool selected_root = false;
        size_t parent_id = -1ull;
        size_t top_selected_count = 0;
    };

    SelectAnalysisResult select_analysis() const;
    template <bool select = true> void select_with_children(size_t id) {
        assert(this->tests.contains(id));

        if constexpr (select) {
            this->tree_view.selected_tests.insert(id);
        } else if (!this->parent_selected(id)) {
            this->tree_view.selected_tests.erase(id);
        }

        NestedTest& nt = this->tests.at(id);

        if (!std::holds_alternative<Group>(nt)) {
            return;
        }

        Group& group = std::get<Group>(nt);

        for (size_t child_id : group.children_ids) {
            assert(this->tests.contains(child_id));
            if constexpr (select) {
                this->tree_view.selected_tests.insert(child_id);
            } else if (!this->parent_selected(id)) {
                this->tree_view.selected_tests.erase(child_id);
            }

            select_with_children<select>(child_id);
        }
    }

    void move_children_up(Group* group);

    void delete_children(const Group* group);
    void delete_test(size_t id);
    void delete_selected();

    void enable_selected(bool enable = true);

    void group_selected(size_t common_parent_id);

    void copy();
    void cut();

    constexpr bool can_paste() const { return this->clipboard.original_size > 0; }
    void paste(Group* group);

    void move(Group* group, size_t idx);
    void sort(Group& group);

    // Returns true when the value should be filtered *OUT*
    bool filter(Group& group);
    bool filter(Test& test);
    bool filter(NestedTest* nt);

    bool save_file(std::ostream&);
    bool open_file(std::istream&);
    void post_open();

    void import_swagger_paths(const nlohmann::json& paths, const nlohmann::json& swagger);
    void import_swagger_servers(const nlohmann::json&);
    void import_swagger(const std::string& filename);

    void export_swagger_paths(nlohmann::json&) const;
    void export_swagger_servers(nlohmann::json&) const;
    void export_swagger(const std::string& filename) const;

    void load_i18n();

    AppState(HelloImGui::RunnerParams* _runner_params, bool unit_testing = false);

    // no copy/move
    AppState(const AppState&) = delete;
    AppState(AppState&&) = delete;
    AppState& operator=(const AppState&) = delete;
    AppState& operator=(AppState&&) = delete;
};

httplib::Client make_client(const std::string& hostname, const ClientSettings& settings);

template <class It> std::vector<size_t> get_tests_to_run(AppState* app, It begin, It end) {
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

bool is_parent_id(const AppState* app, size_t group_id, size_t parent_id);
void iterate_over_nested_children(const AppState* app, size_t* id, size_t* child_idx,
                                  size_t breakpoint_group);

void run_dynamic_tests(AppState* app, const NestedTest& nt);
void run_test(AppState* app, size_t test_id);
bool execute_test(
    AppState* app, const Test* test, size_t test_result_idx, httplib::Client& cli,
    const std::unordered_map<std::string, std::string>* overload_cookies = nullptr);
void run_tests(AppState* app, const std::vector<size_t>& tests);
void rerun_test(AppState* app, TestResult* result);

bool is_test_running(AppState* app, size_t id);

void stop_test(TestResult* result);
void stop_test(AppState* app, size_t id);
void stop_tests(AppState* app);

bool status_match(const std::string& match, int status);
const char* body_match(const VariablesMap& vars, const Test* test,
                       const httplib::Result& result);
const char* header_match(const VariablesMap&, const Test* test,
                         const httplib::Result& result);
bool test_analysis(AppState*, const Test* test, TestResult* test_result,
                   httplib::Result&& http_result, const VariablesMap& vars);

template <class Data, class Process>
void execute_requestable_sync(AppState* app, Requestable<Data>& requestable, HTTPType type,
                              const std::string& hostname, const std::string& destination,
                              const std::string& body, const httplib::Params& params,
                              Process&& process) {
    requestable.status = REQUESTABLE_WAIT;

    httplib::Client cli(hostname);

    cli.set_follow_location(true);

    httplib::Result result;
    std::string dest_params = httplib::append_query_params(destination, params);
    switch (type) {
    case HTTP_GET:
        result = cli.Get(dest_params);
        break;
    case HTTP_POST:
        result = cli.Post(dest_params, body, "application/octet-stream");
        break;
    case HTTP_DELETE:
        result = cli.Delete(dest_params, body, "application/octet-stream");
        break;
    case HTTP_PATCH:
        result = cli.Patch(dest_params, body, "application/octet-stream");
        break;
    default:
        break;
    }

    if (result.error() != httplib::Error::Success) {
        requestable.status = REQUESTABLE_ERROR;
        requestable.error = to_string(result.error());
    } else {
        if (result->status != 200) {
            requestable.status = REQUESTABLE_ERROR;
            if (result->body != "") {
                requestable.error = result->body;
            } else {
                requestable.error = httplib::status_message(result->status);
            }
        } else {
            requestable.status = REQUESTABLE_FOUND;
            requestable.error = "";
            process(requestable, result->body);
        }
    }
}

template <class Data, class Process>
void execute_requestable_async(AppState* app, Requestable<Data>& requestable, HTTPType type,
                               const std::string& hostname, const std::string& destination,
                               const std::string& body, const httplib::Params& params,
                               Process&& process) {
    app->thr_pool.detach_task(
        [app, &requestable, type, hostname, destination, params, body, process]() mutable {
            execute_requestable_sync(app, requestable, type, hostname, destination, body, params,
                                     process);
        });
}

void remote_file_list(AppState* app, bool sync = false, Requestable<std::vector<std::string>>* result = nullptr);
void remote_file_open(AppState* app, const std::string&);
void remote_file_delete(AppState* app, const std::string&, bool sync = false);
void remote_file_rename(AppState* app, const std::string&, const std::string&);
void remote_file_save(AppState* app, const std::string&, bool sync = false, Requestable<bool>* result = nullptr);

// Backup naming scheme
// name_id.wt
struct BackupInfo {
    std::string name;
    uint32_t id;
};

std::optional<BackupInfo> get_backup_info(const std::string& filename);

void make_local_backup(AppState* app);
void make_remote_backup(AppState* app);
void make_backups(AppState* app);

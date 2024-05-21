#pragma once

#include "string"
#include "nlohmann/json.hpp"

struct I18N {
    std::string win_tests;
    std::string win_editor;
    std::string win_results;
    std::string win_logs;

    std::string menu_edit;
    std::string menu_edit_undo;
    std::string menu_edit_redo;

    std::string menu_file;
    std::string menu_file_save_as;
    std::string menu_file_save;
    std::string menu_file_open;
    std::string menu_file_import;
    std::string menu_file_export;

    std::string menu_languages;
    std::string menu_languages_english;
    std::string menu_languages_ukrainian;

    std::string tv_new_group_name;

    std::string tv_edit;
    std::string tv_delete;
    std::string tv_enable;
    std::string tv_disable;
    std::string tv_move;

    std::string tv_copy;
    std::string tv_paste;
    std::string tv_cut;

    std::string tv_new_test;
    std::string tv_new_group;

    std::string tv_group;
    std::string tv_ungroup;

    std::string tv_sort;

    std::string tv_run_tests;

    std::string ed_home;
    std::string ed_home_content;

    std::string ed_name;

    std::string ed_pd_change_hint;

    std::string ed_variables;
    std::string ed_variables_hint;

    std::string ed_cli_title;
    std::string ed_cli_parent_override;
    std::string ed_cli_dynamic;
    std::string ed_cli_dynamic_hint;
    std::string ed_cli_parent_dynamic_hint;

    std::string ed_cli_keep_alive;
    std::string ed_cli_compression;
    std::string ed_cli_redirects;

    std::string ed_cli_auth;
    std::string ed_cli_auth_name;
    std::string ed_cli_auth_password;
    std::string ed_cli_auth_token;

    std::string ed_cli_proxy;
    std::string ed_cli_proxy_host;
    std::string ed_cli_proxy_port;
    std::string ed_cli_proxy_auth;

    std::string ed_cli_reruns;
    std::string ed_cli_timeout;
};

void from_json(const nlohmann::json& j, I18N& i18n) noexcept;

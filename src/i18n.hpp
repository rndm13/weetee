#pragma once

#include "json.hpp"

#include "string"
#include "vector"

#include "hello_imgui/icons_font_awesome_4.h"

struct I18N {
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

    std::string ed_endpoint;
    std::string ed_type;

    std::string ed_rq_request;
    std::string ed_rq_request_text;
    std::string ed_rq_params;
    std::string ed_rq_body;
    std::string ed_rq_body_type;
    std::string ed_rq_cookies;
    std::string ed_rq_headers;
    std::vector<std::string> ed_rq_body_types;

    std::string ed_rs_status;
    std::string ed_rs_response;
    std::string ed_rs_response_text;
    std::string ed_rs_body;
    std::string ed_rs_body_type;
    std::string ed_rs_set_cookies;
    std::string ed_rs_headers;
    std::vector<std::string> ed_rs_body_types;

    std::string ed_pd_name;
    std::string ed_pd_data;
    std::string ed_pd_change_hint;

    std::string ed_mpbd_type;
    std::string ed_mpbd_data;
    std::string ed_mpbd_content_type;
    std::vector<std::string> ed_mpbd_types;

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

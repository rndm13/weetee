#include "json.hpp"

#include "hello_imgui/icons_font_awesome_4.h"
#include "i18n.hpp"

using json = nlohmann::json;

const char* json_format(std::string& input) noexcept {
    json js = json::parse(input, nullptr, false);
    if (js.is_discarded()) {
        return "Invalid JSON";
    }
    input = js.dump(4);
    return nullptr;
}

const char* json_validate(const std::string& expected, const std::string& got) noexcept {
    json json_expected, json_got;

    json_expected = json::parse(expected, nullptr, false);
    if (json_expected.is_discarded()) {
        return "Invalid Expected JSON";
    }

    json_got = json::parse(got, nullptr, false);
    if (json_got.is_discarded()) {
        return "Invalid Response JSON";
    }

    if (json_expected != json_got) {
        return "Unexpected Response JSON";
    }

    return nullptr;
}

#define I18N_LOAD(j, i18n, icon, id)                                                               \
    if ((j).contains(#id)) {                                                                       \
        (i18n.id) = (!std::string(icon).empty()) ? (icon " ") : "";                                \
        (i18n.id) = std::string((j).at(#id));                                                      \
    }

#define I18N_LOAD_VEC(j, i18n, id)                                                                 \
    if ((j).contains(#id)) {                                                                       \
        (i18n.id) = std::vector<std::string>((j).at(#id));                                         \
    }

#define I18N_LOAD_ID(j, i18n, icon, id)                                                            \
    if ((j).contains(#id)) {                                                                       \
        (i18n.id) = (!std::string(icon).empty()) ? (icon " ") : "";                                \
        (i18n.id) += std::string((j).at(#id)) + "###" #id;                                         \
    }

void from_json(const nlohmann::json& j, I18N& i18n) noexcept {
    if (!j.is_object()) {
        return;
    }

    I18N_LOAD_ID(j, i18n, "", win_tests);
    I18N_LOAD_ID(j, i18n, "", win_editor);
    I18N_LOAD_ID(j, i18n, "", win_results);
    I18N_LOAD_ID(j, i18n, "", win_logs);

    I18N_LOAD_ID(j, i18n, "", menu_languages);
    I18N_LOAD_ID(j, i18n, "", menu_languages_english);
    I18N_LOAD_ID(j, i18n, "", menu_languages_ukrainian);

    I18N_LOAD_ID(j, i18n, ICON_FA_EDIT, menu_edit);
    I18N_LOAD_ID(j, i18n, ICON_FA_UNDO, menu_edit_undo);
    I18N_LOAD_ID(j, i18n, ICON_FA_REDO, menu_edit_redo);

    I18N_LOAD_ID(j, i18n, ICON_FA_FILE, menu_file);
    I18N_LOAD_ID(j, i18n, ICON_FA_SAVE, menu_file_save_as);
    I18N_LOAD_ID(j, i18n, ICON_FA_SAVE, menu_file_save);
    I18N_LOAD_ID(j, i18n, ICON_FA_FILE, menu_file_open);
    I18N_LOAD_ID(j, i18n, "", menu_file_import);
    I18N_LOAD_ID(j, i18n, "", menu_file_export);

    I18N_LOAD(j, i18n, "", tv_new_group_name);

    I18N_LOAD_ID(j, i18n, ICON_FA_EDIT, tv_edit);
    I18N_LOAD_ID(j, i18n, "", tv_delete);
    I18N_LOAD_ID(j, i18n, "", tv_enable);
    I18N_LOAD_ID(j, i18n, "", tv_disable);
    I18N_LOAD_ID(j, i18n, ICON_FA_ARROW_RIGHT, tv_move);

    I18N_LOAD_ID(j, i18n, ICON_FA_COPY, tv_copy);
    I18N_LOAD_ID(j, i18n, ICON_FA_CUT, tv_cut);
    I18N_LOAD_ID(j, i18n, ICON_FA_PASTE, tv_paste);

    I18N_LOAD_ID(j, i18n, ICON_FA_ARROW_CIRCLE_DOWN, tv_group);
    I18N_LOAD_ID(j, i18n, ICON_FA_ARROW_CIRCLE_UP, tv_ungroup);

    I18N_LOAD_ID(j, i18n, ICON_FA_PLUS_CIRCLE, tv_new_test);
    I18N_LOAD_ID(j, i18n, ICON_FA_PLUS_SQUARE, tv_new_group);

    I18N_LOAD_ID(j, i18n, ICON_FA_SORT, tv_sort);

    I18N_LOAD_ID(j, i18n, ICON_FA_ROCKET, tv_run_tests);

    I18N_LOAD_ID(j, i18n, "", ed_home);
    I18N_LOAD(j, i18n, "", ed_home_content);

    I18N_LOAD_ID(j, i18n, "", ed_name);

    I18N_LOAD_ID(j, i18n, "", ed_endpoint);
    I18N_LOAD_ID(j, i18n, "", ed_type);

    I18N_LOAD_ID(j, i18n, "", ed_rq_request);
    I18N_LOAD(j, i18n, "", ed_rq_request_text);
    I18N_LOAD_ID(j, i18n, "", ed_rq_params);
    I18N_LOAD_ID(j, i18n, "", ed_rq_body);
    I18N_LOAD_ID(j, i18n, "", ed_rq_body_type);
    I18N_LOAD_ID(j, i18n, "", ed_rq_cookies);
    I18N_LOAD_ID(j, i18n, "", ed_rq_headers);

    I18N_LOAD_ID(j, i18n, "", ed_rs_status);
    I18N_LOAD_ID(j, i18n, "", ed_rs_response);
    I18N_LOAD(j, i18n, "", ed_rs_response_text);
    I18N_LOAD_ID(j, i18n, "", ed_rs_body);
    I18N_LOAD_ID(j, i18n, "", ed_rs_body_type);
    I18N_LOAD_ID(j, i18n, "", ed_rs_set_cookies);
    I18N_LOAD_ID(j, i18n, "", ed_rs_headers);

    I18N_LOAD_ID(j, i18n, "", ed_pd_name);
    I18N_LOAD_ID(j, i18n, "", ed_pd_data);
    I18N_LOAD(j, i18n, "", ed_pd_change_hint);

    I18N_LOAD_ID(j, i18n, "", ed_mpbd_type);
    I18N_LOAD_ID(j, i18n, "", ed_mpbd_data);
    I18N_LOAD_ID(j, i18n, "", ed_mpbd_content_type);
    I18N_LOAD_VEC(j, i18n, ed_mpbd_types);

    I18N_LOAD_ID(j, i18n, "", ed_variables);
    I18N_LOAD(j, i18n, "", ed_variables_hint);

    I18N_LOAD(j, i18n, ICON_FA_COG, ed_cli_title);
    I18N_LOAD_ID(j, i18n, "", ed_cli_parent_override);
    I18N_LOAD_ID(j, i18n, "", ed_cli_dynamic);
    I18N_LOAD(j, i18n, "", ed_cli_dynamic_hint);
    I18N_LOAD(j, i18n, "", ed_cli_parent_dynamic_hint);

    I18N_LOAD_ID(j, i18n, "", ed_cli_keep_alive);
    I18N_LOAD_ID(j, i18n, ICON_FA_COMPRESS, ed_cli_compression);
    I18N_LOAD_ID(j, i18n, ICON_FA_ARROW_RIGHT, ed_cli_redirects);

    I18N_LOAD_ID(j, i18n, "", ed_cli_auth);
    I18N_LOAD_ID(j, i18n, "", ed_cli_auth_name);
    I18N_LOAD_ID(j, i18n, "", ed_cli_auth_password);
    I18N_LOAD_ID(j, i18n, "", ed_cli_auth_token);

    I18N_LOAD_ID(j, i18n, ICON_FA_GLOBE, ed_cli_proxy);
    I18N_LOAD_ID(j, i18n, "", ed_cli_proxy_host);
    I18N_LOAD_ID(j, i18n, "", ed_cli_proxy_port);
    I18N_LOAD_ID(j, i18n, "", ed_cli_proxy_auth);

    I18N_LOAD_ID(j, i18n, ICON_FA_REDO, ed_cli_reruns);
    I18N_LOAD_ID(j, i18n, ICON_FA_HOURGLASS, ed_cli_timeout);
}

#undef I18N_LOAD
#undef I18N_LOAD_ID

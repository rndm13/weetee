#include "i18n.hpp"
#include "hello_imgui/icons_font_awesome_4.h"

#define I18N_LOAD(j, i18n, icon, id)                                                               \
    if ((j).contains(#id)) {                                                                       \
        (i18n.id) = (!std::string(icon).empty()) ? (icon " ") : "";                                \
        (i18n.id) = std::string((j).at(#id));                                                      \
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

    I18N_LOAD(j, i18n, "", ed_pd_change_hint);

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

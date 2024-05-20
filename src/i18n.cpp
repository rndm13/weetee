#include "i18n.hpp"
#include "hello_imgui/icons_font_awesome_4.h"

#define I18N_LOAD(j, i18n, icon, id)                                                               \
    if ((j).contains(#id)) {                                                                       \
        (i18n.id) = (!std::string(icon).empty()) ? (icon " ") : "";                                \
        (i18n.id) += std::string((j).at(#id)) + "###" #id;                                         \
    }

void from_json(const nlohmann::json& j, I18N& i18n) noexcept {
    if (!j.is_object()) {
        return;
    }

    I18N_LOAD(j, i18n, ICON_FA_EDIT, tv_edit);
    I18N_LOAD(j, i18n, "", tv_delete);
    I18N_LOAD(j, i18n, "", tv_enable);
    I18N_LOAD(j, i18n, "", tv_disable);
    I18N_LOAD(j, i18n, ICON_FA_ARROW_RIGHT, tv_move);

    I18N_LOAD(j, i18n, ICON_FA_COPY, tv_copy);
    I18N_LOAD(j, i18n, ICON_FA_CUT, tv_cut);
    I18N_LOAD(j, i18n, ICON_FA_PASTE, tv_paste);

    I18N_LOAD(j, i18n, ICON_FA_ARROW_CIRCLE_DOWN, tv_group);
    I18N_LOAD(j, i18n, ICON_FA_ARROW_CIRCLE_UP, tv_ungroup);

    I18N_LOAD(j, i18n, ICON_FA_PLUS_CIRCLE, tv_new_test);
    I18N_LOAD(j, i18n, ICON_FA_PLUS_SQUARE, tv_new_group);

    I18N_LOAD(j, i18n, ICON_FA_SORT, tv_sort);

    I18N_LOAD(j, i18n, ICON_FA_ROCKET, tv_run_tests);
}

#undef I18N_LOAD

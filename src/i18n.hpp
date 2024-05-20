#pragma once

#include "string"
#include "nlohmann/json.hpp"

struct I18N {
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
};

void from_json(const nlohmann::json& j, I18N& i18n) noexcept;

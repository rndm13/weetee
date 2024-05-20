#include "gui_tests.hpp"

#include "hello_imgui/hello_imgui.h"

#include "hello_imgui/icons_font_awesome_4.h"
#include "imgui.h"

#include "imgui_test_engine/imgui_te_context.h"

#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "app_state.hpp"
#include "test.hpp"
#include "utils.hpp"

#include "cmath"
#include "string"

void register_tests(AppState* app) noexcept {
    ImGuiTestEngine* e = HelloImGui::GetImGuiTestEngine();

    static constexpr const char* root_selectable = "**/##0";

    static constexpr const char* delete_test_selectable = "**/###delete";
    static constexpr const char* add_new_test_selectable = "**/###new_test";
    static constexpr const char* add_new_group_selectable = "**/###new_group";

    static constexpr const char* copy_selectable = "**/###copy";
    static constexpr const char* paste_selectable = "**/###paste";

    static constexpr const char* group_selectable = "**/###group";
    static constexpr const char* ungroup_selectable = "**/###ungroup";

    static constexpr const char* sort_selectable = "**/###sort";

    static constexpr const char* edit_menu_selectable = "**/###edit";
    static constexpr const char* undo_menu_selectable = "**/###undo";
    static constexpr const char* redo_menu_selectable = "**/###redo";

    static constexpr const char* file_menu_selectable = "**/###file";
    static constexpr const char* save_menu_selectable = "**/###save";

    auto tree_view__select_all = [app](ImGuiTestContext* ctx) -> std::vector<size_t> {
        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;
        ctx->KeyDown(ImGuiKey_ModCtrl);
        for (size_t id : top_items) {
            ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
        }
        ctx->KeyUp(ImGuiKey_ModCtrl);
        // IM_CHECK(app->selected_tests.size() == app->tests.size() - 1); // selected everything
        // except root

        return top_items;
    };

    auto tree_view__delete_all = [app, tree_view__select_all](ImGuiTestContext* ctx) {
        ctx->Yield();
        std::vector<size_t> top_items = tree_view__select_all(ctx);
        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick(delete_test_selectable);

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__basic_context = IM_REGISTER_TEST(e, "tree_view", "basic_context");
    tree_view__basic_context->TestFunc = [app, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_test_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(copy_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(paste_selectable);

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__copy_paste = IM_REGISTER_TEST(e, "tree_view", "copy_paste");
    tree_view__copy_paste->TestFunc = [app, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 5; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick(copy_selectable);
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick(paste_selectable);
        }

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__ungroup = IM_REGISTER_TEST(e, "tree_view", "ungroup");
    tree_view__ungroup->TestFunc = [app, tree_view__select_all,
                                    tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 3; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick(copy_selectable);
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick(paste_selectable);
        }

        while (app->tests.size() > 1) {
            std::vector<size_t> top_items = tree_view__select_all(ctx);
            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick(ungroup_selectable);
        }
    };

    ImGuiTest* tree_view__moving = IM_REGISTER_TEST(e, "tree_view", "moving");
    tree_view__moving->TestFunc = [app, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_test_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(sort_selectable);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;

        // test should be last
        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[1])).c_str(),
                                 ("**/##" + to_string(top_items[0])).c_str());

        ctx->ItemDragOverAndHold(("**/##" + to_string(top_items[2])).c_str(),
                                 ("**/##" + to_string(top_items[1])).c_str());

        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick(delete_test_selectable);

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__group_selected = IM_REGISTER_TEST(e, "tree_view", "group_selected");
    tree_view__group_selected->TestFunc = [app, tree_view__select_all,
                                           tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_test_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        auto& root_group = std::get<Group>(app->tests.at(0));

        {
            auto top_items = tree_view__select_all(ctx);

            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick(group_selectable);

            IM_CHECK(root_group.children_ids.size() == 1); // only one item in root
        }

        {
            auto top_items = tree_view__select_all(ctx);

            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick(group_selectable);
            IM_CHECK(root_group.children_ids.size() == 1); // only one item in root
            IM_CHECK(
                std::get<Group>(app->tests.at(root_group.children_ids.at(0))).children_ids.size() ==
                1); // only one item in root child
        }

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__undo_redo = IM_REGISTER_TEST(e, "tree_view", "undo_redo");
    tree_view__undo_redo->TestFunc = [app, tree_view__select_all,
                                      tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_test_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        ctx->ItemClick(edit_menu_selectable);
        ctx->ItemClick(undo_menu_selectable);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 2);

        ctx->ItemClick(edit_menu_selectable);
        ctx->ItemClick(redo_menu_selectable);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->ItemClick(edit_menu_selectable);
        ctx->ItemClick(undo_menu_selectable);

        ctx->ItemClick(edit_menu_selectable);
        ctx->ItemClick(undo_menu_selectable);

        ctx->ItemClick(edit_menu_selectable);
        ctx->ItemClick(undo_menu_selectable);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 0);
    };

    ImGuiTest* tree_view__save_open = IM_REGISTER_TEST(e, "tree_view", "save_open");
    tree_view__save_open->TestFunc = [app, tree_view__select_all,
                                      tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_test_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick(add_new_group_selectable);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        app->filename = "/tmp/test.wt";
        ctx->ItemClick(file_menu_selectable);
        ctx->ItemClick(save_menu_selectable);

        tree_view__delete_all(ctx);

        app->open_file();

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        tree_view__delete_all(ctx);
    };

    // ImGuiTest* editor__test_params = IM_REGISTER_TEST(e, "editor", "test_params");
    // editor__test_params->TestFunc = [app, tree_view__select_all,
    // tree_view__delete_all](ImGuiTestContext* ctx) {
    //     ctx->SetRef("Tests");

    //     ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
    //     ctx->ItemClick(add_new_test_selectable);
    //     IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 1);

    //     ctx->SetRef("");

    //     ctx->ItemClick("**/Type");
    //     ctx->ItemClick("**/Type/POST");

    //     ctx->ItemInput("Endpoint");
    //     ctx->KeyCharsReplace("127.0.0.1:8000/api/param/{id}");

    //     ctx->Yield();

    //     ctx->ItemOpen("Request/request/Parameters");

    //     ctx->ItemInput("$$0/##name");
    //     ctx->KeyCharsReplace("key");
    //     ctx->ItemInput("$$0/##data");
    //     ctx->KeyCharsReplace("value");

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right);
    //     ctx->ItemClick("Disable");
    //     IM_CHECK(!ctx->ItemIsChecked("$$0/##enabled"));

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right);
    //     ctx->ItemClick("Enable");
    //     IM_CHECK(ctx->ItemIsChecked("$$0/##enabled"));

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right);
    //     ctx->ItemClick("Delete");

    //     ctx->TabClose(("$$" +
    //     to_string(std::get<Group>(app->tests[0]).children_ids[0])).c_str());

    //     ctx->SetRef("Tests");
    //     tree_view__delete_all(ctx);
    // };
}

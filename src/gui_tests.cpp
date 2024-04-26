#include "gui_tests.hpp"

#include "hello_imgui/hello_imgui.h"

#include "imgui.h"
#include "imgui_stdlib.h"

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
    const char* root_selectable = "**/##0";

    auto tree_view__select_all = [app](ImGuiTestContext* ctx) -> std::vector<size_t> {
        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;
        ctx->KeyDown(ImGuiKey_ModCtrl);
        for (size_t id : top_items) {
            ctx->ItemClick(("**/##" + to_string(id)).c_str(), ImGuiMouseButton_Left);
        }
        ctx->KeyUp(ImGuiKey_ModCtrl);
        // IM_CHECK(app->selected_tests.size() == app->tests.size() - 1); // selected everything except root

        return top_items;
    };

    auto tree_view__delete_all = [app, tree_view__select_all](ImGuiTestContext* ctx) {
        std::vector<size_t> top_items = tree_view__select_all(ctx);
        ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__basic_context = IM_REGISTER_TEST(e, "tree_view", "basic_context");
    tree_view__basic_context->TestFunc = [app, root_selectable, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Copy");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Paste");

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__copy_paste = IM_REGISTER_TEST(e, "tree_view", "copy_paste");
    tree_view__copy_paste->TestFunc = [app, root_selectable, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 5; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__ungroup = IM_REGISTER_TEST(e, "tree_view", "ungroup");
    tree_view__ungroup->TestFunc = [app, root_selectable, tree_view__select_all, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");
        for (size_t i = 0; i < 3; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Copy");
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
            ctx->ItemClick("**/Paste");
        }

        while (app->tests.size() > 1) {
            std::vector<size_t> top_items = tree_view__select_all(ctx);
            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick("**/Ungroup");
        }
    };

    ImGuiTest* tree_view__moving = IM_REGISTER_TEST(e, "tree_view", "moving");
    tree_view__moving->TestFunc = [app, root_selectable, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Sort");
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
        ctx->ItemClick("**/Delete");

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__group_selected = IM_REGISTER_TEST(e, "tree_view", "group_selected");
    tree_view__group_selected->TestFunc = [app, root_selectable, tree_view__select_all, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        auto& root_group = std::get<Group>(app->tests.at(0));

        {
            auto top_items = tree_view__select_all(ctx);
            
            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick("**/Group Selected");

            IM_CHECK(root_group.children_ids.size() == 1); // only one item in root
        }

        {
            auto top_items = tree_view__select_all(ctx);
            
            ctx->ItemClick(("**/##" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right);
            ctx->ItemClick("**/Group Selected");
            IM_CHECK(root_group.children_ids.size() == 1); // only one item in root
            IM_CHECK(std::get<Group>(app->tests.at(root_group.children_ids.at(0))).children_ids.size() == 1); // only one item in root child
        }

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__undo_redo = IM_REGISTER_TEST(e, "tree_view", "undo_redo");
    tree_view__undo_redo->TestFunc = [app, root_selectable, tree_view__select_all, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        ctx->ItemClick("**/Edit");
        ctx->ItemClick("**/Undo");

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 2);

        ctx->ItemClick("**/Edit");
        ctx->ItemClick("**/Redo");

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->ItemClick("**/Edit");
        ctx->ItemClick("**/Undo");

        ctx->ItemClick("**/Edit");
        ctx->ItemClick("**/Undo");

        ctx->ItemClick("**/Edit");
        ctx->ItemClick("**/Undo");

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 0);
    };

    ImGuiTest* tree_view__save_open = IM_REGISTER_TEST(e, "tree_view", "save_open");
    tree_view__save_open->TestFunc = [app, root_selectable, tree_view__select_all, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef("Tests");

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new test");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right);
        ctx->ItemClick("**/Add a new group");
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        app->filename = "/tmp/test.wt";
        ctx->ItemClick("**/File");
        ctx->ItemClick("**/Save");

        tree_view__delete_all(ctx);

        app->open_file();

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        tree_view__delete_all(ctx);
    };
}

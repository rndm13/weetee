#include "gui_tests.hpp"

#include "hello_imgui/hello_imgui.h"

#include "imgui.h"

#include "imgui_test_engine/imgui_te_context.h"

#include "imgui_test_engine/imgui_te_engine.h"
#include "imgui_test_engine/imgui_te_ui.h"

#include "cmath"
#include "string"

void register_tests(AppState* app) noexcept {
    ImGuiTestEngine* e = HelloImGui::GetImGuiTestEngine();
    static constexpr ImGuiTestOpFlags op_flags = ImGuiTestOpFlags_MoveToEdgeR;

    static constexpr const char* win_tests_selectable = "###win_tests";
    static constexpr const char* root_selectable = "**/###0";

    static constexpr const char* delete_test_selectable = "**/###tv_delete";
    static constexpr const char* add_new_test_selectable = "**/###tv_new_test";
    static constexpr const char* add_new_group_selectable = "**/###tv_new_group";

    static constexpr const char* copy_selectable = "**/###tv_copy";
    static constexpr const char* paste_selectable = "**/###tv_paste";

    static constexpr const char* group_selectable = "**/###tv_group";
    static constexpr const char* ungroup_selectable = "**/###tv_ungroup";

    static constexpr const char* sort_selectable = "**/###tv_sort";

    static constexpr const char* edit_menu_selectable = "**/###menu_edit";
    static constexpr const char* undo_menu_selectable = "**/###menu_edit_undo";
    static constexpr const char* redo_menu_selectable = "**/###menu_edit_redo";

    static constexpr const char* file_menu_selectable = "**/###menu_file";
    static constexpr const char* save_menu_selectable = "**/###menu_file_save";

    auto tree_view__select_all = [app](ImGuiTestContext* ctx) -> std::vector<size_t> {
        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;
        if (top_items.size() > 0) {
            ctx->ItemClick(("**/###" + to_string(top_items.front())).c_str(), ImGuiMouseButton_Left,
                           op_flags);
            ctx->KeyDown(ImGuiKey_ModShift);
            ctx->ItemClick(("**/###" + to_string(top_items.back())).c_str(), ImGuiMouseButton_Left,
                           op_flags);
            ctx->KeyUp(ImGuiKey_ModShift);
        }

        return top_items;
    };

    auto tree_view__delete_all = [app, tree_view__select_all](ImGuiTestContext* ctx) {
        std::vector<size_t> top_items = tree_view__select_all(ctx);
        if (top_items.size() > 0) {
            ctx->ItemClick(("**/###" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right,
                           op_flags);
            ctx->ItemClick(delete_test_selectable, ImGuiMouseButton_Left, op_flags);
        }

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__basic_context = IM_REGISTER_TEST(e, "tree_view", "basic_context");
    tree_view__basic_context->TestFunc = [app, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(copy_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(paste_selectable, ImGuiMouseButton_Left, op_flags);

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__copy_paste = IM_REGISTER_TEST(e, "tree_view", "copy_paste");
    tree_view__copy_paste->TestFunc = [app, tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);
        for (size_t i = 0; i < 5; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(copy_selectable, ImGuiMouseButton_Left, op_flags);
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(paste_selectable, ImGuiMouseButton_Left, op_flags);
        }

        tree_view__delete_all(ctx);
    };

    ImGuiTest* tree_view__ungroup = IM_REGISTER_TEST(e, "tree_view", "ungroup");
    tree_view__ungroup->TestFunc = [app, tree_view__select_all,
                                    tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);
        for (size_t i = 0; i < 3; i++) {
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(copy_selectable, ImGuiMouseButton_Left, op_flags);
            ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(paste_selectable, ImGuiMouseButton_Left, op_flags);
        }

        while (app->tests.size() > 1) {
            std::vector<size_t> top_items = tree_view__select_all(ctx);
            ctx->ItemClick(("**/###" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(ungroup_selectable, ImGuiMouseButton_Left, op_flags);
        }
    };

    auto drag_and_drop = [app](ImGuiTestContext* ctx, ImGuiTestRef ref_src, ImGuiTestRef ref_dst) {
        if (ctx->IsError())
            return;

        ImGuiTestItemInfo* item_src = ctx->ItemInfo(ref_src);
        ImGuiTestItemInfo* item_dst = ctx->ItemInfo(ref_dst);
        ImGuiTestRefDesc desc_src(ref_src, item_src);
        ImGuiTestRefDesc desc_dst(ref_dst, item_dst);
        ctx->LogDebug("ItemDragOverAndHold %s to %s", desc_src.c_str(), desc_dst.c_str());

        ctx->MouseMove(ref_src, op_flags | ImGuiTestOpFlags_NoCheckHoveredId);
        ctx->SleepStandard();
        ctx->MouseDown(0);

        // Enforce lifting drag threshold even if both item are exactly at the same location.
        ctx->MouseLiftDragThreshold();

        ctx->MouseMove(ref_dst, op_flags | ImGuiTestOpFlags_NoCheckHoveredId);
        ctx->SleepNoSkip(1.0f, 1.0f / 10.0f);
        ctx->MouseUp(0);
    };

    ImGuiTest* tree_view__moving = IM_REGISTER_TEST(e, "tree_view", "moving");
    tree_view__moving->TestFunc = [app, tree_view__delete_all,
                                   drag_and_drop](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(sort_selectable, ImGuiMouseButton_Left, op_flags);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        std::vector<size_t> top_items = std::get<Group>(app->tests[0]).children_ids;

        drag_and_drop(ctx, ("**/###" + to_string(top_items[2])).c_str(),
                      ("**/###" + to_string(top_items[0])).c_str());

        drag_and_drop(ctx, ("**/###" + to_string(top_items[1])).c_str(),
                      ("**/###" + to_string(top_items[0])).c_str());

        drag_and_drop(ctx, ("**/###" + to_string(top_items[2])).c_str(),
                      ("**/###" + to_string(top_items[1])).c_str());

        ctx->ItemClick(("**/###" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right,
                       op_flags);
        ctx->ItemClick(delete_test_selectable, ImGuiMouseButton_Left, op_flags);

        IM_CHECK(app->tests.size() == 1); // only root is left
    };

    ImGuiTest* tree_view__group_selected = IM_REGISTER_TEST(e, "tree_view", "group_selected");
    tree_view__group_selected->TestFunc = [app, tree_view__select_all,
                                           tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        auto& root_group = std::get<Group>(app->tests.at(0));

        {
            auto top_items = tree_view__select_all(ctx);

            ctx->ItemClick(("**/###" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(group_selectable, ImGuiMouseButton_Left, op_flags);

            IM_CHECK(root_group.children_ids.size() == 1); // only one item in root
        }

        {
            auto top_items = tree_view__select_all(ctx);

            ctx->ItemClick(("**/###" + to_string(top_items[0])).c_str(), ImGuiMouseButton_Right, op_flags);
            ctx->ItemClick(group_selectable, ImGuiMouseButton_Left, op_flags);
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
        ctx->SetRef(win_tests_selectable);

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        ctx->ItemClick(edit_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(undo_menu_selectable, ImGuiMouseButton_Left);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 2);

        ctx->ItemClick(edit_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(redo_menu_selectable, ImGuiMouseButton_Left);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->ItemClick(edit_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(undo_menu_selectable, ImGuiMouseButton_Left);

        ctx->ItemClick(edit_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(undo_menu_selectable, ImGuiMouseButton_Left);

        ctx->ItemClick(edit_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(undo_menu_selectable, ImGuiMouseButton_Left);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 0);
    };

    ImGuiTest* tree_view__save_open = IM_REGISTER_TEST(e, "tree_view", "save_open");
    tree_view__save_open->TestFunc = [app, tree_view__select_all,
                                      tree_view__delete_all](ImGuiTestContext* ctx) {
        ctx->SetRef(win_tests_selectable);

        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
        ctx->ItemClick(add_new_group_selectable, ImGuiMouseButton_Left, op_flags);
        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        ctx->SetRef("");

        app->local_filename = "/tmp/test.wt";
        ctx->ItemClick(file_menu_selectable, ImGuiMouseButton_Left);
        ctx->ItemClick(save_menu_selectable, ImGuiMouseButton_Left);

        tree_view__delete_all(ctx);

        std::ifstream in(app->local_filename.value());
        app->open_file(in);

        IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 3);

        tree_view__delete_all(ctx);
    };

    // ImGuiTest* editor__test_params = IM_REGISTER_TEST(e, "editor", "test_params");
    // editor__test_params->TestFunc = [app, tree_view__select_all,
    // tree_view__delete_all](ImGuiTestContext* ctx) {
    //     ctx->SetRef(win_tests_selectable);

    //     ctx->ItemClick(root_selectable, ImGuiMouseButton_Right, op_flags);
    //     ctx->ItemClick(add_new_test_selectable, ImGuiMouseButton_Left, op_flags);
    //     IM_CHECK_EQ(std::get<Group>(app->tests[0]).children_ids.size(), 1);

    //     ctx->SetRef("");

    //     ctx->ItemClick("**/Type", op_flags);
    //     ctx->ItemClick("**/Type/POST", op_flags);

    //     ctx->ItemInput("Endpoint", op_flags);
    //     ctx->KeyCharsReplace("127.0.0.1:8000/api/param/{id}");

    //     ctx->Yield();

    //     ctx->ItemOpen("Request/request/Parameters", op_flags);

    //     ctx->ItemInput("$$0/##name", op_flags);
    //     ctx->KeyCharsReplace("key");
    //     ctx->ItemInput("$$0/##data", op_flags);
    //     ctx->KeyCharsReplace("value");

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right, op_flags);
    //     ctx->ItemClick("Disable", op_flags);
    //     IM_CHECK(!ctx->ItemIsChecked("$$0/##enabled", op_flags));

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right, op_flags);
    //     ctx->ItemClick("Enable", op_flags);
    //     IM_CHECK(ctx->ItemIsChecked("$$0/##enabled", op_flags));

    //     ctx->ItemClick("$$0/##element", ImGuiMouseButton_Right, op_flags);
    //     ctx->ItemClick("Delete", op_flags);

    //     ctx->TabClose(("$$" +
    //     to_string(std::get<Group>(app->tests[0]).children_ids[0])).c_str());

    //     ctx->SetRef(win_tests_selectable);
    //     tree_view__delete_all(ctx);
    // };
}

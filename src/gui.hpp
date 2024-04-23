#pragma once

#include "app_state.hpp"
#include "test.hpp"

#include "cmath"

bool tree_view_context(AppState* app, size_t nested_test_id) noexcept;
bool tree_view_selectable(AppState* app, size_t id, const char* label) noexcept;
bool tree_view_show(AppState* app, NestedTest& nt, float indentation) noexcept;
bool tree_view_show(AppState* app, Test& test, float indentation = 0) noexcept;
bool tree_view_show(AppState* app, Group& group, float indentation = 0) noexcept;
void tree_view(AppState* app) noexcept;

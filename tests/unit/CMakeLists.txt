add_executable(utils_test utils.cpp)
target_link_libraries(utils_test
  GTest::gtest_main utils)

add_executable(variables_test variables.cpp)
target_link_libraries(variables_test
  GTest::gtest_main variables)

add_executable(json_test json.cpp)
target_link_libraries(json_test
  GTest::gtest_main json)

add_executable(save_state_test save_state.cpp)
target_link_libraries(save_state_test
  GTest::gtest_main save_state)

gtest_discover_tests(utils_test)
gtest_discover_tests(variables_test)
gtest_discover_tests(json_test)
gtest_discover_tests(save_state_test)

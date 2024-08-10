add_executable(utils_test utils.cpp)
target_link_libraries(utils_test
  GTest::gtest_main utils)

add_executable(variables_test variables.cpp)
target_link_libraries(variables_test
  GTest::gtest_main variables)

gtest_discover_tests(utils_test)
gtest_discover_tests(variables_test)

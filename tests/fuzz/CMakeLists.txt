add_executable(save_state_fuzz save_state.cpp)
target_link_libraries(save_state_fuzz
    save_state app_state
    "-g -O0 -fsanitize=fuzzer,address")

add_test(NAME save_state_fuzz COMMAND "${CMAKE_CURRENT_BINARY_DIR}/save_state_fuzz" "${CMAKE_CURRENT_SOURCE_DIR}/save_state_corpus" "-runs=10000")

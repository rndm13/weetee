add_library(utils STATIC utils.hpp utils.cpp)
target_link_libraries(utils PUBLIC hello_imgui)

add_library(save_state save_state.hpp save_state.cpp)
target_link_libraries(save_state PUBLIC utils hello_imgui portable_file_dialogs)

add_library(json STATIC json.hpp json.cpp)
target_link_libraries(json PUBLIC nlohmann_json::nlohmann_json utils)

add_library(textinputcombo textinputcombo.hpp textinputcombo.cpp)
target_link_libraries(textinputcombo PUBLIC hello_imgui)

add_library(partial_dict partial_dict.hpp partial_dict.cpp)
target_link_libraries(partial_dict PUBLIC textinputcombo hello_imgui save_state)

add_library(http http.hpp http.cpp)
target_link_libraries(http PUBLIC textinputcombo hello_imgui save_state partial_dict)

add_library(test test.hpp test.cpp)
target_link_libraries(test PUBLIC textinputcombo hello_imgui save_state partial_dict http json)

add_library(app_state app_state.hpp app_state.cpp)
target_link_libraries(app_state PUBLIC 
    hello_imgui textinputcombo
    save_state partial_dict test
    json http BS_thread_pool)

add_library(gui gui.hpp gui.cpp)
target_link_libraries(gui PUBLIC 
    textinputcombo
    hello_imgui portable_file_dialogs imspinner
    save_state partial_dict test
    json http BS_thread_pool)

add_library(gui_tests gui_tests.hpp gui_tests.cpp)
target_link_libraries(gui_tests PUBLIC 
    gui textinputcombo
    hello_imgui portable_file_dialogs imspinner
    save_state partial_dict test
    json http BS_thread_pool)

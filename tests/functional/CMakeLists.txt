add_library(gui_tests gui_tests.hpp gui_tests.cpp)
target_link_libraries(gui_tests PUBLIC 
    gui textinputcombo
    hello_imgui portable_file_dialogs imspinner
    save_state partial_dict test
    json http variables BS_thread_pool)

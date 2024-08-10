add_library(utils STATIC utils.hpp utils.cpp)
target_link_libraries(utils PUBLIC httplib::httplib imgui)

add_library(i18n STATIC i18n.hpp i18n.cpp)
target_link_libraries(i18n PUBLIC utils json hello_imgui)

add_library(save_state save_state.hpp save_state.cpp)
target_link_libraries(save_state PUBLIC utils hello_imgui portable_file_dialogs)

add_library(json STATIC json.hpp json.cpp)
target_link_libraries(json PUBLIC i18n utils nljson)

add_library(variables variables.hpp variables.cpp)
target_link_libraries(variables PUBLIC utils json)

add_library(textinputcombo textinputcombo.hpp textinputcombo.cpp)
target_link_libraries(textinputcombo PUBLIC hello_imgui)

add_library(partial_dict partial_dict.hpp partial_dict.cpp)
target_link_libraries(partial_dict PUBLIC hello_imgui save_state portable_file_dialogs)

add_library(http http.hpp http.cpp)
target_link_libraries(http PUBLIC hello_imgui save_state partial_dict)

add_library(tests tests.hpp tests.cpp)
target_link_libraries(tests PUBLIC hello_imgui json save_state partial_dict http json variables)

add_library(app_state app_state.hpp)
target_sources(app_state PUBLIC app_state.cpp app_state_swagger.cpp)
target_link_libraries(app_state PUBLIC 
    i18n
    hello_imgui textinputcombo
    save_state partial_dict tests
    json http variables BS_thread_pool)

add_library(gui gui.hpp gui.cpp)
target_link_libraries(gui PUBLIC 
    textinputcombo imgui_toggle
    hello_imgui portable_file_dialogs imspinner
    save_state partial_dict tests app_state
    json http variables BS_thread_pool)
